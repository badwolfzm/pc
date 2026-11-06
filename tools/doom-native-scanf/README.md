# Genuine Doom freestanding integer sscanf ABI provider

Claim: #240; own only tools/doom-native-scanf/** and its workflow. This is **not** general scanf. Implements exactly six formats in pinned Doomgeneric m_misc.c and m_config.c: " 0x%x", " 0X%x", " 0%o", " %d", "%x" and "%i". Exports __isoc99_sscanf for x86-64 glibc-header compiled source. No heap, syscall, host file operations or new undefined imports.

Unsupported formats return -1 without modifying output. Integer overflow is rejected rather than promising full libc overflow behavior. Valid pointers are required; this is not a capability boundary. Doom's m_config.c ignores the conversion result, so malformed configuration inputs require additional game-owner review.

Host differential tests compare 110 supported-format cases against glibc with undefined-behavior sanitizer and check unsupported formats.

CI pins Doomgeneric e257a09527062bfb986b6e5e0780839b6cbd5b71 and compiles the original 79 real engine translation units with Warden adapters using merged tools/doom-native-link-gate/gate.py. It compiles this new provider freestanding and checks exact __isoc99_sscanf import closure via ld -r, requiring sensitive file/process operations remain unresolved.

This does **not** build a complete executable, boot Doom inside Warden, verify 512 MiB RAM/512 MiB shared GPU or produce FPS. Independent review required. GPL considerations apply to upstream game code, which is not copied into this repo.
