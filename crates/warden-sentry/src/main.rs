//! `warden`: Phase 0 command-line front end.
//!
//!   warden init     --state DIR                 starter manifest and registry
//!   warden add-lib  --state DIR FILE [--wants "PERM ..."]   a downloaded library -> request
//!   warden scan     --state DIR PROG            queue every library PROG needs
//!   warden approve  --state DIR                 checkbox screen for pending requests
//!   warden list     --state DIR                 show approvals
//!   warden run      --state DIR [-v] -- PROG [ARGS...]
//!   warden schema                               print the ABI contract (JSON)

mod elf;
mod filter;
mod policy;
mod supervisor;
mod sys;

use std::ffi::CString;
use std::io::{self, BufRead};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::path::{Path, PathBuf};
use std::process::exit;
use warden_libgrant::{self as lg, Grant, LibRequest};

fn die(msg: impl AsRef<str>) -> ! {
    eprintln!("warden: {}", msg.as_ref());
    exit(2)
}

struct Args {
    state: Option<PathBuf>,
    verbose: bool,
    wants: Option<String>,
    rest: Vec<String>,
}

fn parse(argv: &[String]) -> Args {
    let mut a = Args { state: None, verbose: false, wants: None, rest: Vec::new() };
    let mut i = 0;
    while i < argv.len() {
        match argv[i].as_str() {
            "--state" => {
                i += 1;
                a.state = Some(PathBuf::from(argv.get(i).unwrap_or_else(|| die("--state needs a directory"))));
            }
            "--wants" => {
                i += 1;
                a.wants = Some(argv.get(i).unwrap_or_else(|| die("--wants needs a value")).clone());
            }
            "-v" | "--verbose" => a.verbose = true,
            "--" => {
                a.rest.extend_from_slice(&argv[i + 1..]);
                break;
            }
            s => a.rest.push(s.to_string()),
        }
        i += 1;
    }
    a
}

fn state(a: &Args) -> PathBuf {
    let s = a.state.clone().unwrap_or_else(|| PathBuf::from(".warden"));
    std::fs::create_dir_all(&s).unwrap_or_else(|e| die(format!("{}: {e}", s.display())));
    s
}

const STARTER_MANIFEST: &str = "# App grants: <permission><TAB><scope>. Nothing else is reachable.
fs.read\t/usr/lib
fs.read\t/lib
fs.read\t/lib64
exec\t/usr/bin
";

const STARTER_WANTS: &str = "# What libraries ask for, by name prefix: <prefix><TAB><permission=scope> ...
# Every library also asks for \"load\". Example:
# libcurl.so\tnet.connect=*:443
";

fn cmd_init(a: &Args) {
    let s = state(a);
    for (f, body) in [("manifest.wdn", STARTER_MANIFEST), ("libwants.wdn", STARTER_WANTS)] {
        let p = s.join(f);
        if !p.exists() {
            std::fs::write(&p, body).unwrap_or_else(|e| die(e.to_string()));
            println!("wrote {}", p.display());
        }
    }
}

fn cmd_add_lib(a: &Args) {
    let s = state(a);
    let file = a.rest.first().unwrap_or_else(|| die("add-lib needs a library file"));
    let p = s.join("pending.wdn");
    let mut pending = lg::load_pending(&p).unwrap_or_else(|e| die(e.to_string()));
    let approvals = lg::load_approvals(&s.join("approvals.wdn")).unwrap_or_else(|e| die(e.to_string()));
    match queue_lib(&s, &mut pending, &approvals, file, a.wants.as_deref(), true) {
        Some(msg) => {
            lg::save_pending(&p, &pending).unwrap_or_else(|e| die(e.to_string()));
            println!("queued {msg} for approval; run `warden approve`");
        }
        None => unreachable!(),
    }
}

fn queue_lib(s: &Path, pending: &mut Vec<LibRequest>, approvals: &[lg::Approval], file: &str, extra: Option<&str>, again: bool) -> Option<String> {
    let f = std::fs::File::open(file).unwrap_or_else(|e| die(format!("{file}: {e}")));
    let sha = lg::sha256_reader(f).unwrap_or_else(|e| die(e.to_string()));
    if !again && approvals.iter().any(|a| a.sha256 == sha) {
        return None;
    }
    let name = Path::new(file).file_name().and_then(|n| n.to_str()).unwrap_or(file).to_string();
    let mut wants = lg::wants_for(&s.join("libwants.wdn"), &name).unwrap_or_else(|e| die(e.to_string()));
    for word in extra.unwrap_or("").split_whitespace() {
        let g = Grant::parse(word).unwrap_or_else(|e| die(e));
        if !wants.contains(&g) {
            wants.push(g);
        }
    }
    pending.retain(|q| q.sha256 != sha);
    let abs = std::fs::canonicalize(file).map(|p| p.display().to_string()).unwrap_or(file.to_string());
    pending.push(LibRequest { sha256: sha.clone(), name: name.clone(), path: abs, wants });
    Some(format!("{name} sha256={}", &sha[..16]))
}

fn cmd_scan(a: &Args) {
    let s = state(a);
    let prog = find_program(a.rest.first().unwrap_or_else(|| die("scan needs a program")));
    let libs = elf::closure(&prog).unwrap_or_else(|e| die(e));
    let p = s.join("pending.wdn");
    let mut pending = lg::load_pending(&p).unwrap_or_else(|e| die(e.to_string()));
    let approvals = lg::load_approvals(&s.join("approvals.wdn")).unwrap_or_else(|e| die(e.to_string()));
    let mut queued = 0;
    for (soname, path) in libs {
        if path.is_empty() {
            println!("  {soname}: not found in {}", elf::LIB_DIRS.join(":"));
            continue;
        }
        if let Some(msg) = queue_lib(&s, &mut pending, &approvals, &path, None, false) {
            println!("  queued {msg}");
            queued += 1;
        } else {
            println!("  {soname}: already decided");
        }
    }
    lg::save_pending(&p, &pending).unwrap_or_else(|e| die(e.to_string()));
    println!("{queued} library request(s) queued for {prog}; run `warden approve`");
}

fn cmd_approve(a: &Args) {
    let s = state(a);
    let pp = s.join("pending.wdn");
    let ap = s.join("approvals.wdn");
    let pending = lg::load_pending(&pp).unwrap_or_else(|e| die(e.to_string()));
    if pending.is_empty() {
        println!("No pending library requests.");
        return;
    }
    let stdin = io::stdin();
    let mut input = stdin.lock();
    let mut out = io::stdout();
    match lg::checklist(&pending, &mut input, &mut out).unwrap_or_else(|e| die(e.to_string())) {
        None => println!("\nCancelled; nothing saved."),
        Some(result) => {
            let mut approvals = lg::load_approvals(&ap).unwrap_or_else(|e| die(e.to_string()));
            for r in result {
                let shown: Vec<_> = r.granted.iter().map(Grant::render).collect();
                println!("{}: {}", r.name, if shown.is_empty() { "nothing granted (will be refused)".into() } else { shown.join(", ") });
                lg::upsert(&mut approvals, r);
            }
            lg::save_approvals(&ap, &approvals).unwrap_or_else(|e| die(e.to_string()));
            lg::save_pending(&pp, &[]).unwrap_or_else(|e| die(e.to_string()));
            println!("Saved to {}", ap.display());
        }
    }
    let _ = input.fill_buf();
}

fn cmd_list(a: &Args) {
    let s = state(a);
    for r in lg::load_approvals(&s.join("approvals.wdn")).unwrap_or_else(|e| die(e.to_string())) {
        let g: Vec<_> = r.granted.iter().map(Grant::render).collect();
        println!("{}  {}  {}", &r.sha256[..16], r.name, g.join(" "));
    }
}

fn find_program(p: &str) -> String {
    if p.contains('/') {
        return std::fs::canonicalize(p).map(|x| x.display().to_string()).unwrap_or(p.to_string());
    }
    for dir in std::env::var("PATH").unwrap_or_default().split(':') {
        let c = Path::new(dir).join(p);
        if c.is_file() {
            return c.display().to_string();
        }
    }
    die(format!("{p}: not found in PATH"))
}

fn cmd_run(a: &Args) {
    let s = state(a);
    if a.rest.is_empty() {
        die("run needs a program: warden run --state DIR -- PROG ARGS");
    }
    let manifest = lg::load_manifest(&s.join("manifest.wdn")).unwrap_or_else(|e| die(format!("manifest: {e}")));
    let mut pol = policy::Policy::new();
    for g in &manifest {
        if let Err(e) = pol.add(g) {
            die(e);
        }
    }
    let prog_path = find_program(&a.rest[0]);
    let prog = CString::new(prog_path.clone()).unwrap();
    let argv: Vec<CString> = a.rest.iter().map(|x| CString::new(x.as_str()).unwrap()).collect();
    let mut argv_p: Vec<*const libc::c_char> = argv.iter().map(|c| c.as_ptr()).collect();
    argv_p.push(std::ptr::null());
    let envs: Vec<CString> = std::env::vars().map(|(k, v)| CString::new(format!("{k}={v}")).unwrap()).collect();
    let mut env_p: Vec<*const libc::c_char> = envs.iter().map(|c| c.as_ptr()).collect();
    env_p.push(std::ptr::null());
    let bpf = filter::build();

    let mut up = [0i32; 2];
    let mut down = [0i32; 2];
    unsafe {
        if libc::pipe2(up.as_mut_ptr(), libc::O_CLOEXEC) != 0 || libc::pipe2(down.as_mut_ptr(), libc::O_CLOEXEC) != 0 {
            die("pipe failed");
        }
    }
    let pid = unsafe { libc::fork() };
    if pid < 0 {
        die("fork failed");
    }
    if pid == 0 {
        // Child: no allocation from here on.
        unsafe {
            libc::setpgid(0, 0);
            let fd = sys::install_filter(&bpf) as i32;
            if fd < 0 {
                libc::_exit(126);
            }
            libc::write(up[1], &fd as *const i32 as *const _, 4);
            let mut go = 0u8;
            libc::read(down[0], &mut go as *mut u8 as *mut _, 1);
            // The sandbox must never hold its own listener: with it, it could
            // answer its own notifications.
            libc::close(fd);
            libc::execve(prog.as_ptr(), argv_p.as_ptr(), env_p.as_ptr());
            libc::_exit(127);
        }
    }
    unsafe {
        libc::close(up[1]);
        libc::close(down[0]);
    }
    let mut fdnum = -1i32;
    if unsafe { libc::read(up[0], &mut fdnum as *mut i32 as *mut _, 4) } != 4 || fdnum < 0 {
        die("sandbox setup failed: could not install seccomp filter");
    }
    let pidfd = sys::pidfd_open(pid).unwrap_or_else(|e| die(format!("pidfd_open: {e}")));
    let listener: OwnedFd = sys::pidfd_getfd(pidfd.as_raw_fd(), fdnum).unwrap_or_else(|e| die(format!("pidfd_getfd: {e}")));
    let mut sup = supervisor::Supervisor::new(listener, pol, &s, pid, a.verbose).unwrap_or_else(|e| die(e.to_string()));
    unsafe {
        libc::write(down[1], b"g".as_ptr() as *const _, 1);
        libc::close(down[1]);
        drop(OwnedFd::from_raw_fd(up[0]));
    }
    sup.serve();
    let mut status = 0;
    unsafe { libc::waitpid(pid, &mut status, 0) };
    let code = if libc::WIFEXITED(status) { libc::WEXITSTATUS(status) } else { 128 + libc::WTERMSIG(status) };
    if a.verbose {
        eprintln!(
            "warden: exit {code}; {} mediated calls allowed, {} denied, {} new library requests, {} approvals on file",
            sup.allowed, sup.denied, sup.new_requests, sup.approvals_count()
        );
    }
    exit(code);
}

fn main() {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    let (cmd, rest) = argv.split_first().unwrap_or_else(|| die("usage: warden <init|add-lib|approve|list|run|schema> ..."));
    let a = parse(rest);
    match cmd.as_str() {
        "init" => cmd_init(&a),
        "add-lib" => cmd_add_lib(&a),
        "approve" => cmd_approve(&a),
        "scan" => cmd_scan(&a),
        "list" => cmd_list(&a),
        "run" => cmd_run(&a),
        "schema" => print!("{}", warden_abi::schema_json()),
        other => die(format!("unknown command {other}")),
    }
}
