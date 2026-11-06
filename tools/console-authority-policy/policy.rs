//! Reference policy for a future explicit Warden console object grant.
 //! Not wired into the kernel: syscall dispatch currently remains vulnerable.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind { Endpoint, Host, Frame }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Denial { InsufficientRights, WrongKind }
/// The console object ID is trusted kernel state, never supplied by guest args.
/// A revoked cap must be rejected by the capability table before calling this.
pub fn authorize_console(kind: Kind, object_id: u64, can_write: bool,
                         designated_console_id: Option<u64>) -> Result<(), Denial> {
    if !can_write { return Err(Denial::InsufficientRights); }
    match designated_console_id {
        Some(expected) if kind == Kind::Endpoint && object_id == expected => Ok(()),
        _ => Err(Denial::WrongKind),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn designated_endpoint_write_passes() {
        assert_eq!(authorize_console(Kind::Endpoint, 7, true, Some(7)), Ok(()));
    }
    #[test] fn unrelated_endpoint_denied_even_with_write() {
        assert_eq!(authorize_console(Kind::Endpoint, 9, true, Some(7)), Err(Denial::WrongKind));
    }
    #[test] fn no_console_authority_fails_closed() {
        assert_eq!(authorize_console(Kind::Endpoint, 7, true, None), Err(Denial::WrongKind));
    }
    #[test] fn wrong_kind_denied_even_if_object_number_matches() {
        for kind in [Kind::Host,Kind::Frame] {
            assert_eq!(authorize_console(kind, 7, true, Some(7)),Err(Denial::WrongKind));
        }
    }
    #[test] fn read_only_denied() {
        assert_eq!(authorize_console(Kind::Endpoint,7,false,Some(7)),Err(Denial::InsufficientRights));
    }
}
