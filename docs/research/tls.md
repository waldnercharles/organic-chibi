# TLS: vendor a library, write one in chibi, or call the platform

Research note, 2026-11-27. Question: which TLS implementation should chibi use, first for its own git client, which fetches pinned dependencies over HTTPS from GitHub, GitLab, Codeberg and self-hosted servers, and later for HTTPS requests from games through the standard library? The options are a C library vendored into what chibi ships, TLS written in chibi as Zig wrote `std.crypto.tls`, and the TLS that Windows and macOS include. [libraries.md](../libraries.md) lists this under "Not intended yet", and the dependencies row of 2026-11-26 in [decisions.md](../decisions.md) has the first version run the `git` command, so nothing here is needed before chibi's own client.

**Scope.** Client only: no server and no client certificates. Windows first, then Linux and macOS on arm64. For each candidate: license, TLS versions, code lines of what a client needs, dependencies, how it builds on Windows, certificate verification and the system root store, maintenance and recent CVEs, side-channel posture, and how well it suits chibi's C header import or a port. Section 2 measures what the three hosts require. Section 3 covers Zig in depth. GPL, LGPL and commercial-only libraries are out, because chibi is moving to a permissive license. Two long-term goals weigh on the choice: the fastest compiler possible, and chibi working without any C compiler (the Compiler row of 2026-11-09, whose fourth step allows prebuilt archives of C libraries).

**Sources.** I read each project's source at the commits below, from shallow or sparse clones, and counted lines myself. "Code lines" come from scc 4.1.0 and leave out blank lines and comments. Zig keeps its tests in the source files, so for Zig I removed `test` blocks before counting; that removed 4,274 lines. "Lines" come from `wc -l`. Section 2 lists the commands I ran against the hosts. Maintenance facts come from release pages, security advisories and issue trackers, linked where used. Source text is paraphrased, not quoted. Claims I could not read directly are marked "(unverified)" and listed in section 9. "My analysis" marks reasoning, and "my arithmetic" marks numbers I derived.

| Project | Commit, date | Project | Commit, date |
|---|---|---|---|
| Zig 0.17.1 (Codeberg) | `be248cc1`, 2026-11-02 | Zig master | `e7325096`, 2026-11-27 |
| Go 1.27.4 | `7b288102`, 2026-11-10 | Odin master | `19039b3e`, 2026-11-27 |
| BearSSL | `7bea48e5`, 2026-04-06 | Mbed TLS 3.6.8 | `82e69999`, 2026-11-09 |
| Mbed TLS 4.3.0 | `eafb4a72`, 2026-11-09 | TF-PSA-Crypto 1.3.0 | `1161f3fc`, 2026-11-04 |
| s2n-tls 1.7.14 | `03698619`, 2026-11-10 | OpenSSL 4.1.0 | `2f4e3a5f`, 2026-10-23 |
| LibreSSL 4.4.1 | release tarball, SHA-256 `290cdf1b…` | BoringSSL | `a0035907`, 2026-11-26 |
| AWS-LC 5.15.0 | `4378134b`, 2026-11-16 | picotls | `e999a3d9`, 2026-11-17 |
| rustls 0.23.46 | `f04804fa`, 2026-10-13 | rustls-ffi 0.15.7 | `8f0eadf9`, 2026-10-26 |
| wolfSSL 5.9.4 | `3c5eead4`, 2026-09-25 | TLSe | `00e187f6`, 2026-11-25 |
| Cute Framework (`cute_tls.h` 1.02) | `6b00978a`, 2026-11-04 | curl 8.23.0 | `3163ec06`, 2026-10-14 |

Also read: Odin dev-2026-11a `9a699168`, odin-http `5e301bef`, Hare 0.26.0.1 `1593e405`, hare-tls `dfdb9004`, C3 0.8.6 `f86ceff5`, reqwest `de553734`, rustls-platform-verifier `6e4c1c4f`, x509-limbo `b1242378`, and Microsoft's and Apple's pages, linked where used.

## 1. Summary

### 1.1 Recommendations, ranked by correctness and security first

1. **Use the platform's TLS for the git client: Schannel on Windows, Network.framework on macOS, and the distribution's OpenSSL 3 on Linux.** The operating system's vendor patches it, and it uses the OS's root store, enterprise roots and distrust decisions. chibi vendors no TLS code, so the license, the size and the no-C-compiler goal are not affected. The costs are three backends that behave differently, TLS 1.2 only on Windows 10, and Apple blocks, which chibi can't call yet (sections 5 and 8).
2. **If chibi vendors a library, vendor rustls through rustls-ffi, with its platform verifier, as a prebuilt archive; if the archive must build without Rust, vendor Mbed TLS.** The no-C-compiler goal allows either as a prebuilt archive. rustls is memory-safe, has 13 RustSec advisories since 2024 across rustls, its verifier and rustls-ffi, asks the OS to verify the chain on Windows and macOS, and has a C header of opaque handles that suits the C import. Building it needs Rust, cargo-c and a C compiler, because its default crypto, aws-lc-rs, compiles AWS-LC, which is large. Mbed TLS is Apache-2.0, needs only a C99 compiler, and has long-term-support branches, but it published 53 advisories in 2026, among them a basicConstraints bug that made a leaf certificate look like a CA, and it reads no system root store (section 4).
3. **TLS written in chibi is reasonable later, ported from Go's `crypto/tls` and `crypto/x509`, not from Zig.** My estimate is about 15,000 hand-written lines plus about 10,000 generated lines of field arithmetic (section 7). Build it after the compiler is self-hosted, once chibi has a 64×64→128-bit multiply, a constant-time barrier, and AES and carry-less multiply instructions. Ship it only after it passes x509-limbo, Wycheproof and BoringSSL's BoGo suite, and keep the platform verifier on Windows and macOS even then.
4. **Do not port Zig's TLS as it stands.** Zig 0.17.1 never reads the basicConstraints extension, so it accepts a chain in which an ordinary leaf certificate signs a certificate for any other host. Anyone with a valid certificate for one domain can impersonate GitHub to a Zig client, on every platform. The report has been open since 2026-06-21, and master still has the bug today (section 3.6).
5. **Rule out** s2n-tls (it needs a separate libcrypto, and Windows works only through MSYS2 with AWS-LC), picotls with minicrypto (it can't verify a server's signature), TLSe (its TLS 1.3 client is experimental), wolfSSL (GPL-3.0 or commercial) and BoringSSL (no API stability).

### 1.2 The options ranked

My analysis, in the order the question gives: correctness and security, then size, license, dependencies, and fit with the two goals.

| Rank | Option | Security | Code chibi owns or ships | License | Dependencies | Goals |
|---|---|---|---|---|---|---|
| 1 | Platform TLS (Schannel, Network.framework, distro OpenSSL 3) | OS vendor patches it; OS trust store | about 1,200 lines of glue, as in `cute_tls.h` | none to ship | the OS; `libssl.so.3` on Linux | No C compiler, except Apple blocks; compile speed unaffected |
| 2 | rustls through rustls-ffi, prebuilt | memory-safe Rust; aws-lc-rs crypto; platform verifier built in | 39,000 Rust and header lines plus aws-lc-rs | Apache-2.0, ISC or MIT | Rust, cargo-c and a C compiler to build the archive | prebuilt archive only |
| 3 | Mbed TLS 3.6 or 4.x, prebuilt | C; 53 advisories in 2026; team-maintained | 77,000 to 90,000 code lines | Apache-2.0 | none | prebuilt archive |
| 4 | TLS in chibi, from Go | new code; Zig shows the risk | about 25,000 lines | chibi's own | none | best fit, once self-hosted |
| 5 | BearSSL, prebuilt | constant-time by design; TLS 1.2 only | 16,000 code lines | MIT | none | prebuilt archive |
| 6 | OpenSSL, AWS-LC or LibreSSL, vendored | most deployed; large | 140,000 to 440,000 code lines | Apache-2.0 or ISC-style | Perl, NASM, CMake, Go | prebuilt archive |
| 7 | Zig's TLS, ported | open MITM bug | 25,100 code lines | MIT | none | fits, after a port |

### 1.3 Terms

| Term | Meaning |
|---|---|
| TLS 1.2, TLS 1.3 | The two versions of the Transport Layer Security protocol still in use. TLS 1.3 (RFC 8446) has a shorter handshake and drops the old ciphers. |
| Handshake | The messages at the start of a connection that agree on keys and prove the server's identity. |
| Cipher suite | The set of algorithms one connection uses. In TLS 1.3 it names only the AEAD and the hash, such as `TLS_AES_128_GCM_SHA256`. |
| AEAD | A cipher that encrypts and authenticates together: AES-GCM, ChaCha20-Poly1305. |
| Key exchange, group | How client and server agree on a shared secret: X25519, or ECDHE on the curve P-256 or P-384. |
| X25519MLKEM768 | A hybrid key exchange that adds ML-KEM-768, a post-quantum algorithm, to X25519. |
| Signature scheme | How the server signs the handshake and a CA signs a certificate: ECDSA on P-256 or P-384, RSA with PKCS#1 v1.5 or PSS padding, Ed25519. |
| X.509 certificate, chain | The signed document that binds a public key to a host name, and the list from the server's certificate up to a root. |
| Root store, trust anchor | The set of root certificates a client trusts. Windows, macOS and Linux distributions each keep one. |
| Path validation | Checking a chain: each signature, each validity period, that each issuer is a CA (basicConstraints), key usage, name constraints, and the host name. |
| basicConstraints | The X.509 extension that says whether a certificate may sign other certificates (the cA bit) and how long a chain below it may be. |
| Platform verifier | Asking the OS to validate a chain, as Go does on Windows and macOS, instead of validating it in your own code. |
| SNI, ALPN | Extensions that tell the server which host name, and which application protocol such as HTTP/2, the client wants. |
| HelloRetryRequest (HRR) | A server's request that the client repeat its first message with a different key share. |
| Session resumption | Reusing keys from an earlier connection to skip part of the handshake. |
| Revocation | Withdrawing a certificate before it expires, checked through CRLs or OCSP. |
| Constant-time code | Code whose timing and memory access do not depend on secret values. |
| Schannel, SSPI | Windows' TLS implementation, and the Security Support Provider Interface through which C code calls it. |
| Network.framework | Apple's connection API, which includes TLS. Its callbacks are Apple blocks. |
| Block | Apple's C extension for closures, written `^(…) { … }`. A call needs a block object built to Apple's block ABI. |

## 2. What GitHub, GitLab.com and Codeberg require

I ran these from the owner's Linux machine on 2026-11-27, with OpenSSL 3.5.9 and curl:

- `openssl s_client -connect HOST:443 -servername HOST -tls1`, then `-tls1_1`, both with `-cipher 'DEFAULT:@SECLEVEL=0'` so that the local library would offer them, then `-tls1_2` and `-tls1_3`.
- `openssl s_client … -showcerts` for the chain, with `-sigalgs 'rsa_pss_rsae_sha256:rsa_pkcs1_sha256'` or `-sigalgs 'ecdsa_secp256r1_sha256:ecdsa_secp384r1_sha384'` to ask for each key type.
- `openssl s_client … -groups X25519`, then `P-256`, `P-384` and `X25519MLKEM768`, one at a time.
- `openssl s_client … -tls1_2 -cipher SUITE` for each of seven TLS 1.2 suites.
- `curl -sv --tlsv1.3 https://github.com/ziglang/zig.git/info/refs?service=git-upload-pack`, the first request of a git fetch over HTTPS.

| | github.com | gitlab.com | codeberg.org |
|---|---|---|---|
| TLS 1.0, 1.1 | refused, `protocol_version` alert | refused | refused |
| TLS 1.2 | yes | yes | yes |
| TLS 1.3 | yes, `TLS_AES_128_GCM_SHA256` | yes, `TLS_AES_256_GCM_SHA384` | yes, `TLS_AES_256_GCM_SHA384` |
| Leaf key by default | ECDSA P-256 | RSA 2048 | ECDSA P-256 |
| Leaf key when only RSA is offered | RSA 4096 | RSA 2048 | handshake failure |
| Leaf key when only ECDSA is offered | ECDSA P-256 | handshake failure | ECDSA P-256 |
| Chain served | leaf, Sectigo DV E36 (P-256), Sectigo Root E46 (P-384) cross-signed by USERTrust ECC | leaf, Sectigo DV R36 (RSA 3072), Sectigo Root R46 (RSA 4096) cross-signed by USERTrust RSA | leaf, Let's Encrypt YE1 (P-384), ISRG Root YE (P-384) cross-signed by ISRG Root X2, X2 cross-signed by ISRG Root X1 (RSA) |
| Groups accepted alone | X25519, P-256; not P-384 or X25519MLKEM768 | all four | all four |
| Group chosen by default | X25519 | X25519MLKEM768 | X25519MLKEM768 |
| TLS 1.2 suites accepted | ECDHE-ECDSA and ECDHE-RSA with AES-GCM or ChaCha20; plain RSA key exchange `AES128-GCM-SHA256` | ECDHE-RSA with AES-GCM, ChaCha20 or AES-CBC; `AES128-GCM-SHA256` | ECDHE-ECDSA with AES-GCM or ChaCha20 only |
| ALPN | `h2` accepted (curl) | not tested | not tested |

What a client needs, from the table (my analysis):

- **TLS 1.3 alone reaches all three hosts.** So do git.sr.ht, gitea.com, bitbucket.org, codeload.github.com and objects.githubusercontent.com, which all negotiated TLS 1.3 by default. TLS 1.2 matters for self-hosted servers and for Windows 10's Schannel.
- **Both key types.** GitLab serves only RSA and Codeberg only ECDSA, so a client verifies RSA (PKCS#1 v1.5 in certificates, PSS in the TLS 1.3 handshake, keys of 2048 to 4096 bits) and ECDSA on P-256 and P-384. Codeberg's intermediate signs with P-384.
- **A chain longer than three, with cross-signs.** Codeberg's chain reaches its new root, ISRG Root YE, through two cross-signs, so a client must stop at the first trusted issuer rather than demand the whole chain.
- **X25519 or P-256 for key exchange, and AES-128-GCM.** Every host accepts both.
- **What TLS protects in a git fetch.** A pinned commit is checked by its hash (the dependencies row of 2026-11-26), so TLS guards the first fetch, before a pin exists, and any credentials for private repositories. A broken verifier turns the printed pin into a pin of whatever a man in the middle served.

## 3. Zig's `std.crypto.tls`

### 3.1 Versions and history

- TLS 1.3 only in Zig 0.13.0 (2024-06-06), whose ClientHello lists no other version ([Client.zig L164-L167 at 0.13.0](https://codeberg.org/ziglang/zig/src/commit/cf90dfd3098bef5b3c22d5ab026173b3c357f2dd/lib/std/crypto/tls/Client.zig#L164-L167)).
- TLS 1.2 since Zig 0.14.0 (2025-03-04). Jacob Young added it on 2024-11-01 ([c2a779ae](https://github.com/ziglang/zig/commit/c2a779ae79facc0c6a102825723ae707fdbf8c19)), and added the P-384 key share and the full signature list in the following week.
- At 0.17.1, `Client.init` negotiates TLS 1.3 or 1.2 ([Client.zig L190-L215](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L190-L215)).

### 3.2 What it offers

| | Offered at 0.17.1 | Source |
|---|---|---|
| TLS 1.3 suites | AEGIS-128L, AEGIS-256, AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305; ChaCha20 first when the CPU has no AES instructions | [L1651-L1672](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1651-L1672) |
| TLS 1.2 suites | ECDHE-RSA with AES-128-GCM, AES-256-GCM or ChaCha20-Poly1305. No ECDHE-ECDSA suite, so Zig can't use TLS 1.2 with Codeberg | same lines |
| Groups | X25519MLKEM768, P-256, P-384, X25519. The client sends a key share for all four in every ClientHello and generates all four key pairs | [L212-L247](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L212-L247), [L1353-L1376](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1353-L1376) |
| Signature schemes | ECDSA P-256 and P-384, RSA PKCS#1 v1.5 (SHA-1 to SHA-512), RSA-PSS (rsae and pss), Ed25519 | [L216-L229](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L216-L229) |
| Certificate signatures | RSA with SHA-1 to SHA-512, ECDSA with SHA-224 to SHA-512, Ed25519; MD2 and MD5 refused | [Certificate.zig L259-L305](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate.zig#L259-L305) |

### 3.3 System root certificates

`Certificate.Bundle.rescan` picks a loader by OS ([Bundle.zig L75-L93](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Bundle.zig#L75-L93)). `std.http.Client` rescans on the first HTTPS request ([http/Client.zig L1714-L1733](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/http/Client.zig#L1714-L1733)).

- **Windows.** It copies every certificate in the `ROOT` system store through `CertOpenSystemStoreW` and `CertEnumCertificatesInStore`, then verifies with its own code ([L149-L174](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Bundle.zig#L149-L174)). Since PR [#31592](https://codeberg.org/ziglang/zig/pulls/31592) (merged 2026-03-20), a chain that reaches no copied root goes to `CertGetCertificateChain` and `CertVerifyCertificateChainPolicy` with the SSL policy, which also makes Windows download a missing root; Zig then rescans ([Chain.zig L43-L88](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Chain.zig#L43-L88), [Client.zig L1606-L1627](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1606-L1627)).
- **macOS.** It parses two keychain files itself, `/System/Library/Keychains/SystemRootCertificates.keychain` and `/Library/Keychains/System.keychain`, with no call to Security.framework, so user trust settings and distrust decisions are ignored ([macos.zig L11-L30](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Bundle/macos.zig#L11-L30)).
- **Linux.** It reads the first of six fixed PEM bundle paths that exists, or else every file in three directories ([L96-L138](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Bundle.zig#L96-L138)). `SSL_CERT_FILE` and `SSL_CERT_DIR` are ignored ([#36288](https://codeberg.org/ziglang/zig/issues/36288), open).
- **All platforms.** The bundle keeps one root per subject name and drops the rest, so a store with two roots of the same name and different keys fails depending on order ([L310-L315](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Bundle.zig#L310-L315), [#37750](https://codeberg.org/ziglang/zig/issues/37750), open).

### 3.4 Code lines

Code lines with tests removed, at 0.17.1 ([lib/std/crypto](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto)). The primitives are the files a client reaches, whole.

| Part | Files | Code lines |
|---|---|---|
| TLS client | `tls.zig`, `tls/Client.zig` | 2,092 |
| X.509 parsing and verification, with its DER reader and RSA | `Certificate.zig` | 986 |
| Root stores and the Windows chain check | `Certificate/Bundle.zig`, `Bundle/macos.zig`, `Certificate/Chain.zig` | 447 |
| AES with AES-NI, ARMv8, table and bitsliced software paths, GCM, GHASH, CTR | 8 files | 2,894 |
| AEGIS | `aegis.zig` | 619 |
| ChaCha20, Poly1305 | 2 files | 742 |
| SHA-1, SHA-2, HMAC, HKDF | 4 files | 962 |
| X25519 and Ed25519 | 6 files | 2,051 |
| P-256, P-384, ECDSA | 12 files | 11,634, of which 9,934 are fiat-crypto output |
| RSA big integers | `ff.zig` | 931 |
| ML-KEM-768, SHA-3, Keccak | 3 files | 1,629 |
| Constant-time compare, errors | 2 files | 160 |
| **Total** | | **25,147** (my arithmetic) |

A client that drops AEGIS, ML-KEM and Ed25519 needs about 22,100 (my arithmetic), and 9,934 of those are generated field arithmetic.

### 3.5 Limits

- **No HelloRetryRequest.** The client rejects one as an unexpected message, which is why it sends four key shares ([L454-L458](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L454-L458)).
- **No session resumption.** It ignores NewSessionTicket ([L1261-L1263](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1261-L1263)).
- **No client certificates.** A certificate_request is unsupported; PR [#37480](https://codeberg.org/ziglang/zig/pulls/37480) is open.
- **No revocation** in its own verifier. Only the Windows fallback asks for an end-certificate check, and it ignores an unknown status ([Chain.zig L52-L81](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate/Chain.zig#L52-L81)).
- **No ALPN**, so no HTTP/2. The ClientHello carries no ALPN extension ([L212-L247](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L212-L247)).
- **No KeyUpdate on overflow.** The send path leaves it as a TODO ([L1070](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1070)).
- **No P-521.** A corporate proxy whose CA uses P-521 breaks every connection ([#37736](https://codeberg.org/ziglang/zig/issues/37736), open).
- **Variable-time ECDH on P-256 and P-384.** The key exchange multiplies by the client's secret scalar with `mulPublic`, which the curve code documents as variable-time and only for public scalars ([L1398-L1415](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/tls/Client.zig#L1398-L1415), [p256.zig L408-L410](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/pcurves/p256.zig#L408-L410)). The scalar is used once, which limits the leak.
- **Software AES is only partly constant-time.** Without AES instructions, AES-GCM runs bitsliced code under the default `side_channels_mitigations = .medium`, but the one-block round function, which AEGIS uses, reads tables in a pattern that the setting only partly hides; the client lists ChaCha20 first on such CPUs ([soft.zig L608-L626, L868-L895](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/aes/soft.zig#L868-L895), [crypto.zig L245-L265](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto.zig#L245-L265)).

### 3.6 Security bugs

Zig has no security policy. On 2026-11-23 Andrew Kelley answered a request for a private reporting channel with "just file an issue like normal" ([#37740](https://codeberg.org/ziglang/zig/issues/37740)). I found no CVE for `std.crypto` (unverified).

| Bug | Reported | State | Source |
|---|---|---|---|
| **The verifier never checks the cA bit or the path length.** `Certificate.parse` keeps only the subjectAltName extension and skips the rest, and `verify` checks only issuer name, validity and signature. A leaf certificate for `attacker.example` can sign one for `github.com`, and the client accepts the chain | 2024-04-26 as a comment on GitHub [#14175](https://github.com/ziglang/zig/issues/14175); 2026-06-21 as [#35877](https://codeberg.org/ziglang/zig/issues/35877); again as [#37353](https://codeberg.org/ziglang/zig/issues/37353) | Open. Fixes [#35951](https://codeberg.org/ziglang/zig/pulls/35951) closed unmerged; [#36211](https://codeberg.org/ziglang/zig/pulls/36211) and [#37354](https://codeberg.org/ziglang/zig/pulls/37354) open; an earlier fix, GitHub [#19759](https://github.com/ziglang/zig/pull/19759), closed after two days. Unfixed at master `e7325096` | [Certificate.zig L552-L585, L255-L270](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/crypto/Certificate.zig#L552-L585) |
| Key usage, extended key usage, name constraints and unknown critical extensions are not checked | 2023-01-03, [#14175](https://github.com/ziglang/zig/issues/14175) | Open | same lines |
| TLS 1.3 accepted PKCS#1 v1.5 and SHA-1 in CertificateVerify, which RFC 8446 forbids | 2026-11-24, with its fix [#37757](https://codeberg.org/ziglang/zig/pulls/37757) | Fixed on master 2026-11-26, after 0.17.1 | |
| Modular exponentiation branched on the secret exponent, because the compiler turned constant-time selects into jumps | 2026-09-30, [#37023](https://codeberg.org/ziglang/zig/issues/37023) | Closed 2026-10-04 | |
| ECDSA DER parser accepts non-minimal integers | 2026-07-14, [#36156](https://codeberg.org/ziglang/zig/issues/36156) | Open | |
| `std.http.Client` may draw TLS entropy from a non-cryptographic source | 2026-11-23, [#37743](https://codeberg.org/ziglang/zig/issues/37743) | Open | |
| RSA-PSS verification panic | 2026-08-08, [#36440](https://codeberg.org/ziglang/zig/pulls/36440) | Fixed | |
| UTCTime years misread | [#31981](https://codeberg.org/ziglang/zig/pulls/31981) | Fixed 2026-04-20 | |

Two fetch failures are open: GitLab archives ([#30853](https://codeberg.org/ziglang/zig/issues/30853)) and GitHub on Windows 10 ([#36842](https://codeberg.org/ziglang/zig/issues/36842)) fail with `TlsInitializationFailed`.

### 3.7 What chibi could take from Zig

Zig is a fair guide to size: about 3,500 lines of TLS, X.509 and root-store code on top of about 21,600 lines of primitives, which the git fetcher of Zig 0.17.1 uses through `std.http.Client` with no OpenSSL ([Maker/Fetch.zig L1219-L1263](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/compiler/Maker/Fetch.zig#L1219-L1263)). It is not a guide to correctness. A team that includes experienced cryptographers shipped a verifier without the cA check in every release since the gap was pointed out in April 2024, five minor releases from 0.13 to 0.17, and left it open five months after a public report with a fix attached. My analysis: the handshake is the part that interoperability testing catches; path validation fails silently, and only adversarial test suites catch it.

## 4. Libraries

### 4.1 Comparison

"Client code lines" counts what a client compiles; section 4.2 says what each count covers.

| Library | License | TLS 1.3 / 1.2 | Client code lines | Dependencies | Windows build | Root store | Latest release, cadence | Advisories since 2024 |
|---|---|---|---|---|---|---|---|---|
| Mbed TLS 3.6 LTS | Apache-2.0 OR GPL-2.0-or-later | yes / yes | 89,810 (library subset) | none | Makefile or Visual Studio solution; or list the `.c` files | none; files only | 3.6.8, 2026-11-16; three release days a year; LTS to March 2027 | 71 for both lines: 7 in 2024, 11 in 2025, 53 in 2026 |
| Mbed TLS 4.3 + TF-PSA-Crypto 1.3 | Apache-2.0 OR GPL-2.0-or-later | yes / yes, ECDHE only | 77,288 | none | CMake only; release tarballs carry generated files | none | 4.3.0, 2026-11-16; 4.1 LTS to March 2029 | same |
| BearSSL | MIT | no / yes | 16,110 (one 64-bit implementation of each primitive) | none | `nmake`; or list the `.c` files | none; trust anchors as C data | v0.6, 2018-08-14; 39 commits since, last 2026-04-06 | no CVEs; one buffer overflow fixed 2026-04-06 |
| s2n-tls | Apache-2.0 | yes / yes | 39,774 plus a libcrypto | AWS-LC, OpenSSL, BoringSSL or LibreSSL | MSYS2 and MinGW with AWS-LC only; no MSVC | system store on Unix | 1.7.14, 2026-11-13; every 2–3 weeks | 7 |
| OpenSSL 4.1 | Apache-2.0 | yes / yes | 57,624 in libssl; 441,177 C with libcrypto and providers | Perl, NASM to build | Perl `Configure`, NASM, `nmake` | `org.openssl.winstore` loader on Windows | 4.1.0, 2026-10-23; April and October; 3.5 LTS to 2030-04-08 | 65 CVE-2026 entries on its page |
| LibreSSL 4.4 (libtls) | ISC for new code, OpenSSL/SSLeay for inherited code | yes / yes | 198,224 (libtls, libssl, libcrypto, headers) | none | CMake; bash for git builds | a CA file at a fixed path | 4.4.1, 2026-11-11; April and October | no index found |
| BoringSSL | Apache-2.0 | yes / yes | 140,560 C++ and headers | C++17, NASM, CMake | MSVC 2022, CMake | none | dated snapshots only; no API stability | 1 that applied to it |
| AWS-LC 5.15 | Apache-2.0 OR ISC, plus inherited licenses | yes / yes | 210,142 C, C++ and headers, plus generated assembly | CMake, Perl, Go, NASM | CMake | none | 5.15.0, 2026-11-16; every one to two weeks; LTS to about 2030 | 6, all in 2026 |
| picotls (minicrypto) | MIT, with CC0 and BSD-2 dependencies | yes / no | 7,413 core plus 14,761 minicrypto | none | Visual Studio 2017 solution | none | no releases; last commit 2026-11-17 | 4, among them a 2026 client crash after HRR |
| rustls 0.23 via rustls-ffi | Apache-2.0 OR ISC OR MIT | yes / yes | 33,255 rustls plus 5,570 rustls-ffi Rust lines, plus webpki and aws-lc-rs | Rust 1.85+, cargo-c; a C compiler for aws-lc-rs | MSVC | platform verifier built in | 0.23.46, 2026-10-14; rustls-ffi 0.15.7, 2026-10-26 | 13 RustSec across rustls, webpki and rustls-ffi |
| TLSe | BSD-2-Clause OR Unlicense | experimental client / yes | 13,411 plus 20,791 libtomcrypt | libtomcrypt, last released 2018 | CMake or one file | none | last tag 2018; commits in 2026 | open heap overflows and fuzzing crashes |
| wolfSSL | GPL-3.0 or commercial | yes / yes | out on license | | | | 5.9.4, 2026-09-25 | 97 CVEs listed in 5.7.0 to 5.9.4 |

### 4.2 Notes on each

- **Mbed TLS.**
  - Features in 3.6: TLS 1.3 and 1.2 client with SNI, ALPN, session tickets, HelloRetryRequest and early data; groups X25519, P-256, P-384, P-521, X448 and FFDHE; the TLS 1.3 client needs the PSA crypto layer ([tls13-support.md](https://github.com/Mbed-TLS/mbedtls/blob/82e69999f91126589a28b70dd75b69fbcabe2711/docs/architecture/tls13-support.md)). No ML-KEM in 4.3. Its X.509 code parses basicConstraints, path length and extended key usage ([x509_crt.c L495-L587](https://github.com/Mbed-TLS/mbedtls/blob/82e69999f91126589a28b70dd75b69fbcabe2711/library/x509_crt.c#L495-L587)).
  - 4.0 (2025-10-15) moved the crypto into TF-PSA-Crypto, a submodule, made PSA the only crypto API, and removed TLS 1.2's RSA, DHE-RSA, PSK variants and static ECDH key exchanges ([ChangeLog L326-L452](https://github.com/Mbed-TLS/mbedtls/blob/eafb4a72592addfd5ba1326caad2f3e9adef49ad/ChangeLog#L326-L452)). It builds only with CMake 3.20.2 or later, and a git checkout needs Python and Perl to generate files that release tarballs include ([README.md L36-L86](https://github.com/Mbed-TLS/mbedtls/blob/eafb4a72592addfd5ba1326caad2f3e9adef49ad/README.md#L36-L86)).
  - Support: 3.6 LTS until March 2027, 4.1 LTS until March 2029, a new LTS every 18 months ([BRANCHES.md L26-L30](https://github.com/Mbed-TLS/mbedtls/blob/eafb4a72592addfd5ba1326caad2f3e9adef49ad/BRANCHES.md#L26-L30)).
  - Security: 53 advisories in 2026, 22 of them on 2026-11-16 ([index](https://mbed-tls.readthedocs.io/en/latest/security-advisories/)). Two that matter for a client: a malformed basicConstraints extension made a leaf read as a CA ([CVE-2026-49300](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-07-x509-ca-bit-forgery-invalid-basicconstraints/), 2026-07-07), and a crafted ServerHello could finish a TLS 1.3 handshake with no secret keys ([CVE-2026-115490](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-11-tls-version-confusion/), 2026-11-16). Its policy promises full protection against remote timing attacks and only limited protection against local ones ([SECURITY.md L59-L75](https://github.com/Mbed-TLS/mbedtls/blob/eafb4a72592addfd5ba1326caad2f3e9adef49ad/SECURITY.md#L59-L75)), and one open advisory says clang turns its constant-time padding checks into branches ([CVE-2025-66442](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-03-compiler-induced-constant-time-violations/)).
  - C import: plain structs and functions, with I/O through send and receive callbacks. The struct layouts depend on the configuration header, so the bindings must come from the same configuration as the archive. `ssl.h` alone marks 209 fields `MBEDTLS_PRIVATE` and holds 22 `static inline` functions.
  - Counts: 3.6.8's TLS client files are 23,962 code lines and its X.509 parsing and verification 9,138; the 89,810 leave out server, DTLS cookie, ticket, cache, certificate writing and unused ciphers. 4.3's TLS client is 20,891, X.509 4,104, and the TF-PSA-Crypto subset 52,293 (my arithmetic: 77,288). Both compile every file and strip by configuration macros, so a trimmed configuration compiles less.
- **BearSSL.**
  - TLS 1.0 to 1.2 only, with no TLS 1.3 and no plans for it in the code. ECDHE-ECDSA and ECDHE-RSA with AES-GCM and ChaCha20, so it reaches all three hosts today.
  - Its "minimal" X.509 engine enforces basicConstraints and key usage, rejects unknown critical extensions, and skips name constraints and revocation; the chain must validate in the order sent ([bearssl_x509.h L59-L119](https://www.bearssl.org/gitweb/?p=BearSSL;a=blob;f=inc/bearssl_x509.h;hb=7bea48e5e850ab4cafbe68d3765cdaba13a86d6f#l59)).
  - Constant-time by design for its default AES, RSA and EC code, and it allocates nothing ([constanttime.html](https://bearssl.org/constanttime.html), undated). It has no ARMv8 AES path, so arm64 uses the slower bitsliced AES.
  - One maintainer, Thomas Pornin. Its last commit fixed a buffer overflow in CBC record handling that Anthropic reported on 2026-04-06 ([ANT-2026-45SRWS4Q](https://red.anthropic.com/2026/cvd/findings/ANT-2026-45SRWS4Q), disclosed 2026-08-17).
  - C import: callers allocate every context, and the engine is a buffer state machine that fits any transport. The headers hold 75 `static inline` functions, which the import must translate. The handshake and X.509 code is generated from a Forth-like language, T0 (`ssl_hs_client.t0`, `x509_minimal.t0`), so a port starts from the generated C or from T0.
  - Counts: 16,110 code lines for a 64-bit client with one implementation of each primitive, of which 4,424 are TLS and X.509; 35,810 with every implementation and no server.
- **s2n-tls.** It does TLS itself and takes all primitives from a libcrypto: AWS-LC (required for post-quantum), OpenSSL 1.0.2 to 3.0, BoringSSL or LibreSSL ([BUILD.md L140-L160](https://github.com/aws/s2n-tls/blob/036986196aeb7b9921943f71efccebe4219640af/docs/BUILD.md#L140-L160)). Windows works only through MSYS2 with MinGW and AWS-LC, never MSVC ([BUILD.md L72-L101](https://github.com/aws/s2n-tls/blob/036986196aeb7b9921943f71efccebe4219640af/docs/BUILD.md#L72-L101)). CF's note that s2n "has no Windows support" is out of date since that page appeared on 2026-07-10.
- **OpenSSL.** The most deployed and the most fuzzed. Too large to vendor for one client: 441,177 C and header lines, plus Perl-generated assembly. As the system library on Linux it costs chibi nothing (section 5.3). Its policy places side channels from the same machine outside its threat model ([security policy](https://openssl-library.org/policies/general/security-policy/index.html), undated). Its 2026 CVEs are mostly Low, many in QUIC, CMS and CMP ([vulnerabilities](https://openssl-library.org/news/vulnerabilities/index.html), read 2026-11-27).
- **LibreSSL.** libtls is the cleanest C API of the group, 4,458 code lines over libssl and libcrypto, but it loads roots only from a CA file at a fixed path (`tls/tls_config.c` lines 38–44 in the [4.4.1 tarball](https://cdn.openbsd.org/pub/OpenBSD/LibreSSL/libressl-4.4.1.tar.gz)) and needs CMake on Windows.
- **BoringSSL and AWS-LC.** BoringSSL says it is not for general use and offers no API or ABI stability ([README.md](https://boringssl.googlesource.com/boringssl/+/a003590712db258f80de823b660ee788589c98cf/README.md)), and its `ssl/` is now C++. AWS-LC is the stable fork, with FIPS certificates, formally verified assembly from s2n-bignum, and LTS lines, but its build needs CMake, Perl, Go and NASM.
- **picotls.** Its minicrypto backend can sign but can't verify a peer's signature, so it can't authenticate a server; only the OpenSSL backend verifies certificates ([README.md L30-L33](https://github.com/h2o/picotls/blob/e999a3d9980843905b6c3b0f519c8a59fa45efdb/README.md#L30-L33)).
- **rustls.** Memory-safe, with TLS 1.2 and 1.3. Its default crypto provider has been aws-lc-rs since 0.23.0 (2024-02-29), with post-quantum preferred ([Cargo.toml L18](https://github.com/rustls/rustls/blob/f04804fa13341a9b1dc02b5d1dd8a746f2c4b7fb/rustls/Cargo.toml#L18)). rustls-ffi exposes a C API and `rustls_platform_server_cert_verifier`, which uses the OS verifier ([client.rs L256-L257](https://github.com/rustls/rustls-ffi/blob/8f0eadf96fa21ade96d867c7ea06f8f5fddda856/librustls/src/client.rs#L256-L257)). Its header holds 24 opaque types and 5 plain structs. Building it needs Rust 1.85 or later and cargo-c, and CMake only for the C examples ([README.md L22-L36](https://github.com/rustls/rustls-ffi/blob/8f0eadf96fa21ade96d867c7ea06f8f5fddda856/README.md#L22-L36)).
- **TLSe.** One C file over a libtomcrypt amalgamation. Its README calls the TLS 1.3 client experimental ([README.md](https://github.com/eduardsui/tlse/blob/00e187f69f29d0a41d56fe747905b8ef778ef5c3/README.md)), and it began rejecting off-curve ECDHE points only on 2026-11-25 ([c5446f22](https://github.com/eduardsui/tlse/commit/c5446f22)).
- **wolfSSL.** GPL-3.0 since 5.8.2, with a GPL-2.0 exception for named projects and a commercial license for everyone else ([LICENSING](https://github.com/wolfSSL/wolfssl/blob/3c5eead44904df64e6a5a1f4ebdce377d35a849a/LICENSING)).

## 5. Platform TLS

### 5.1 Windows: Schannel

- TLS 1.3 only on Windows 11 and Server 2022 or later. No Windows 10 release has it, and Microsoft calls enabling it there unsafe ([Protocols in TLS/SSL](https://learn.microsoft.com/en-us/windows/win32/secauthn/protocols-in-tls-ssl--schannel-ssp-), updated 2025-03-20). Windows 10 support ended on 2025-10-14 ([lifecycle](https://learn.microsoft.com/en-us/lifecycle/products/windows-10-home-and-pro)).
- TLS 1.3 also needs the `SCH_CREDENTIALS` structure. `SCHANNEL_CRED` is deprecated ([SCHANNEL_CRED](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-schannel_cred), updated 2024-10-03), and curl's Schannel backend notes that Schannel never negotiates TLS 1.3 through it ([schannel.c L612-L660](https://github.com/curl/curl/blob/3163ec065643ba20709f190111f42c3e1fe95273/lib/vtls/schannel.c#L612-L660)).
- Verification uses the Windows root store, its automatic root updates and enterprise policy. Revocation checks are flags.
- The API is plain C through SSPI: the caller owns the socket and passes byte buffers to `InitializeSecurityContext`, `EncryptMessage` and `DecryptMessage`. That fits the C import and any transport.

### 5.2 macOS: Network.framework

- Apple says not to use Secure Transport, deprecated since 2019 and without TLS 1.3, and points to Network.framework or URLSession ([TN3151](https://developer.apple.com/documentation/technotes/tn3151-choosing-the-right-networking-api)). TLS 1.3 has been the default in Network.framework since iOS 12.2 ([Apple Platform Security](https://support.apple.com/guide/security/sec100a75d12/web)).
- Network.framework owns the whole connection, DNS and TCP included, and reports through blocks on a dispatch queue. `cute_tls.h` passes three blocks, to `nw_connection_set_state_changed_handler`, `nw_connection_receive` and `nw_connection_send`, and says it must be compiled as Objective-C ([cute_tls.h L763, L999, L1657](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_tls.h#L999)).
- My analysis: chibi can call it without C only after it can build a block object with captured state to Apple's block ABI. Until then, a small shim in a prebuilt archive does it. Security.framework's `SecTrustEvaluateWithError` is plain C with CoreFoundation types, so a verifier needs no blocks.

### 5.3 Linux

Linux has no TLS in the kernel API that a client can use for the handshake. The practical "platform TLS" is the distribution's OpenSSL 3, `libssl.so.3`, which the distribution patches. It reads the distribution's root store through `SSL_CTX_set_default_verify_paths`. My analysis: loading it at run time keeps chibi's binary independent of it and needs no C compiler, but a minimal container may lack it, and an old distribution may have only OpenSSL 1.1.

### 5.4 Platform verifiers without platform TLS

Go, rustls-platform-verifier and, since March 2026, Zig's fallback ask the OS to validate the chain while doing TLS themselves:

- Go calls `CertGetCertificateChain` on Windows ([root_windows.go L248-L250](https://github.com/golang/go/blob/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/x509/root_windows.go#L248-L250)) and `SecTrustEvaluateWithError` on macOS since Go 1.18 ([root_darwin.go L52](https://github.com/golang/go/blob/4aa1efed4853ea067d665a952eee77c52faac774/src/crypto/x509/root_darwin.go#L52)), and reads PEM bundles from fixed paths on Linux ([root_linux.go L10-L23](https://github.com/golang/go/blob/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/x509/root_linux.go#L10-L23)). The two platform files are 211 and 105 code lines.
- rustls-platform-verifier uses the OS verifier on Windows, macOS, iOS and Android, and webpki with the system bundle on Linux ([README.md L18-L39](https://github.com/rustls/rustls-platform-verifier/blob/6e4c1c4fbb9ec5355fcf505995724d6aa9ebf84b/README.md#L18-L39)).

This splits the risk: the OS does path validation, which is where Zig failed, and the library only has to get the handshake and the record layer right.

### 5.5 What `cute_tls.h` does

CF's wrapper is 1,165 code lines for four backends: Schannel, Network.framework, Android's `SSLSocket` through JNI, and s2n-tls. Two findings:

- **Its Windows path never negotiates TLS 1.3.** It fills a `SCHANNEL_CRED` and asks for TLS 1.2 and 1.3 ([L915-L924](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_tls.h#L915-L924)), so per section 5.1 it gets TLS 1.2 even on Windows 11. It still verifies the server, through `SCH_CRED_AUTO_CRED_VALIDATION`.
- **Its s2n path creates no `s2n_config`.** It relies on the default configuration, which loads the system trust store ([s2n.h L283](https://github.com/aws/s2n-tls/blob/036986196aeb7b9921943f71efccebe4219640af/api/s2n.h#L283)) and checks the name set by `s2n_set_server_name`.

## 6. What other languages do

| Language | TLS | Root store | Source |
|---|---|---|---|
| Go | Its own, TLS 1.0 to 1.3, client minimum 1.2 by default, X25519MLKEM768 on by default since 1.24. 7,662 client code lines in `crypto/tls` and 4,958 in the verifier, parser and root files of `crypto/x509` (my count) | platform verifier on Windows and macOS; PEM files on Linux. Go 1.27 falls back to its own verifier when `SSL_CERT_FILE` or `SSL_CERT_DIR` is set | [common.go L795-L805](https://github.com/golang/go/blob/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/tls/common.go#L795-L805), [defaults.go L16-L34](https://github.com/golang/go/blob/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/tls/defaults.go#L16-L34), [Go 1.27 notes](https://go.dev/doc/go1.27) |
| Rust | rustls is the default in reqwest since 0.13.0 (2025-12-30), with aws-lc-rs and rustls-platform-verifier. cargo uses libcurl, with Schannel on Windows | platform verifier | [reqwest CHANGELOG L39-L45](https://github.com/seanmonstar/reqwest/blob/de55373434f07f42926599dbb5a88550d8e55112/CHANGELOG.md#L39-L45), [curl-sys build.rs L342-L358](https://github.com/alexcrichton/curl-rust/blob/bae5dc561e0711b4982eeddda8b5dbac45d531f1/curl-sys/build.rs#L342-L358) |
| Zig | Its own, TLS 1.3 and 1.2 (section 3) | copies of the OS stores; Windows chain check as a fallback | section 3.3 |
| Odin | The latest release, dev-2026-11a, has primitives, RSA and X.509 in `core:crypto` but no TLS. Yawning's `core:crypto/tls`, a TLS 1.3-only blocking client, merged on 2026-11-13: 2,959 code lines, with 2,093 in `core:crypto/x509`, which checks name constraints and fails on unknown critical extensions. `vendor:curl` ships a prebuilt libcurl with Schannel for Windows. odin-http uses OpenSSL 3 and never turns on certificate verification | the caller passes the roots | [PR #8397](https://github.com/odin-lang/Odin/pull/8397), [tls_client.odin L27-L48](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/crypto/tls/tls_client.odin#L27-L48), [x509 doc.odin L32-L55](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/crypto/x509/doc.odin#L32-L55), [vendor/curl](https://github.com/odin-lang/Odin/blob/9a6991685ddf663956d9836602eb421e141fc443/vendor/curl/curl.odin#L5-L35), [odin-http client.odin L95-L114](https://github.com/laytan/odin-http/blob/5e301bef8fbee138548661a92104cb1529eb5e39/client/client.odin#L95-L114) |
| Hare | None in 0.26.0.1; `crypto::x509` and ML-KEM are on master. hare-tls by Armin Preiml is TLS 1.3 only, AES-256-GCM only, and calls itself insecure. Hare doesn't target Windows | | [hare-tls README](https://git.sr.ht/~apreiml/hare-tls/tree/dfdb90042b95cd093373cf492f528a7cf524af1b/item/README.md) |
| Jai | A Curl module over libcurl; no TLS of its own; users write OpenSSL bindings (community sources) | libcurl's | [The Way to Jai 34A L286-L350](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/book/34A_Other_useful_modules.md#L286-L350) |
| C3 | None; `std::net` has sockets only, and HTTPS is on a list of project ideas. A vendor binding links the system curl | | [project_suggestions.md L3](https://github.com/c3lang/c3c/blob/f86ceff5b52f9fc7316a3dd2f89f84c4c689c2bf/project_suggestions.md#L3) |

On Odin, Jeroen called TLS "a man year" of work, and Yawning named the WebPKI as the main obstacle ([Odin forum](https://forum.odin-lang.org/t/will-odin-have-its-own-tls-library-like-golang/288), 2024-11-05 to 11-08). Two years later Odin's TLS still leaves root loading to the caller.

## 7. Rebuilding TLS in chibi

### 7.1 Which reference

- **Go** (BSD-3-Clause) is the reference to port. Its client and verifier are mature, it runs BoringSSL's BoGo suite against `crypto/tls` (the `bogo_config.json` in [crypto/tls](https://github.com/golang/go/tree/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/tls)), its verifier builds paths and checks name constraints ([verify.go](https://github.com/golang/go/blob/7b2881024c95a11ddb86663a616340aa3a88b67f/src/crypto/x509/verify.go)), and its P-256 and P-384 field code comes from fiat-crypto, as Zig's does. Go is garbage-collected and uses interfaces, so a port rewrites ownership and dispatch, not just syntax.
- **Zig** (MIT) is the size reference and a closer fit to chibi's style: caller-supplied buffers, no hidden allocation, comptime-built messages. Its verifier must not be copied (section 3.6).
- **Odin's new client** is the closest in spirit, a systems language with explicit allocators, but it is TLS 1.3 only, 14 days old, and loads no roots.
- **BearSSL** is the reference for constant-time primitives in C with no allocation, under MIT.

### 7.2 Size

My estimate, from the counts in sections 3.4 and 6:

| Part | Basis | Lines |
|---|---|---|
| TLS 1.3 and 1.2 client: handshake, key schedule, records, HRR, KeyUpdate, ALPN | Zig 2,092; Go client 7,662; Odin TLS 1.3 only 2,959 | 3,000 |
| X.509 parsing and path validation with basicConstraints, key usage, EKU, name constraints and path building | Go's `verify.go` 992 and `parser.go` 1,181; Zig 986 without the checks | 2,500 |
| Root stores and platform verifiers for three OSes | Zig 447; Go's two platform files 316 | 600 |
| AES-GCM with AES-NI, ARMv8 and a constant-time software path; ChaCha20-Poly1305; SHA-2; HMAC; HKDF | Zig 4,598 | 4,500 |
| X25519; P-256 and P-384 ECDH and ECDSA verify; RSA verify to 4,096 bits | Zig 3,866 without the generated code and Ed25519 | 4,500 |
| Generated P-256 and P-384 field arithmetic | Zig 9,934 from fiat-crypto | 10,000 generated |
| ML-KEM-768 and SHA-3, optional | Zig 1,629 | 1,600 |
| **Total** | | **about 15,000 hand-written to 17,000 with ML-KEM, plus 10,000 generated** |

fiat-crypto emits C, Go, Rust and Zig. chibi's C-to-chibi translator could take its C output, which is straight-line integer arithmetic, once chibi has a wide multiply ([translator.md](translator.md) notes that chibi lacks 128-bit integers). A generic Montgomery field over `u64` limbs would replace the 10,000 generated lines with about 1,500 slower ones (my analysis, from BearSSL's generic `ec_prime_i31.c`).

### 7.3 What chibi needs first

- A 64×64→128-bit multiply, or its high half, for field and RSA arithmetic. Carries need no builtin: `sum := x +% y`, then `carry := sum < x`, which clang compiles to one `adds` ([simd-and-builtins.md](simd-and-builtins.md) section 4).
- A barrier that stops the optimizer from turning branch-free selects into branches. Zig's modular exponentiation and Mbed TLS's padding checks both lost constant time this way in 2026 ([#37023](https://codeberg.org/ziglang/zig/issues/37023), [CVE-2025-66442](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-03-compiler-induced-constant-time-violations/)). chibi's own optimizer must respect it too.
- AES and carry-less multiply instructions: AES-NI and PCLMULQDQ on x86-64, the ARMv8 AES and PMULL instructions on arm64. Without them AES-GCM is slow or leaks through tables.
- A cryptographic random source per OS: `BCryptGenRandom`, `getrandom`, `arc4random_buf`.

### 7.4 Tests it must pass

- [x509-limbo](https://github.com/C2SP/x509-limbo/tree/b12423782a88abead01141fa6582254e3eb49e74), a C2SP suite of path-validation cases that includes Netflix's BetterTLS. Its basicConstraints cases would have caught Zig's bug (my analysis).
- [Wycheproof](https://github.com/C2SP/wycheproof), test vectors against known attacks on each primitive.
- BoGo, BoringSSL's TLS test runner, which Go and rustls run against their stacks.
- Live handshakes with the hosts in section 2, in CI on Windows, Linux and macOS.

## 8. Recommendations

### 8.1 (c) Platform TLS on Windows and macOS: yes, for the git client, with OpenSSL 3 on Linux

My analysis. The git client runs on developers' machines and fetches from public hosts that all accept TLS 1.2 and 1.3 with ordinary chains (section 2). Platform TLS gives it:

- the best-maintained implementation on each OS, patched by Microsoft, Apple and the distributions;
- the OS's root store, corporate roots and distrust decisions, which no library chibi vendors gets without platform calls;
- nothing to vendor, so no license, no size and no C compiler; the compiler's speed is unaffected, because TLS runs only on a cache miss.

It costs:

- **Three backends.** Each fails differently and offers different features, and CI has to run all three. `cute_tls.h` shows the size: 1,165 code lines for four.
- **TLS 1.2 on Windows 10.** Acceptable today: every host in section 2 accepts TLS 1.2. Use `SCH_CREDENTIALS` so that Windows 11 gets TLS 1.3, which `cute_tls.h` misses.
- **Blocks on macOS.** Calling Network.framework from chibi needs Apple's block ABI. Until chibi has it, a shim in a prebuilt archive, as CF compiles `cute_tls.h` as Objective-C.
- **`libssl.so.3` on Linux.** A missing library is a clear error, not a silent failure. A distribution without OpenSSL 3 needs a vendored fallback or a chibi TLS.
- **Games.** For later HTTPS from games, the same backends keep games small and patched by the OS, and extend to Android and iOS as `cute_tls.h` does. Behavior varies by OS version, which a game's support team then sees.

### 8.2 (a) The library to vendor, if chibi vendors one: rustls through rustls-ffi, or Mbed TLS

By the question's order, correctness and security first, rustls through rustls-ffi ranks first. It is memory-safe, its advisories since 2024 number 13 against Mbed TLS's 71, it ships the platform verifier, so path validation is the OS's job on Windows and macOS, and its header, `rustls.h`, declares 24 opaque types and 5 plain structs, so the C import sees no layout that depends on a configuration. It costs a Rust toolchain, cargo-c and a C compiler on the machine that builds the prebuilt archives, about 39,000 lines of its own plus aws-lc-rs, and its crate dependencies to track. Users of chibi never build it, so the no-C-compiler goal holds.

If the archives must build from C alone, Mbed TLS is next: Apache-2.0, TLS 1.3 and 1.2, no dependencies, MSVC and clang without CMake in 3.6, a team, and LTS branches. Use 4.1 LTS or later by the time the client exists, since 3.6 LTS ends in March 2027; 4.x needs CMake, which only the machine that builds the archives runs. Pair it with the platform verifier on Windows and macOS, so that path validation, where Mbed TLS had CVE-2026-49300, is the OS's job there.

Pick BearSSL only if TLS 1.2 alone is acceptable and the smallest size matters most: 16,110 code lines, constant-time, but no TLS 1.3, a 2018 release and one maintainer.

### 8.3 (b) Rebuilding TLS in chibi: reasonable later, from Go, about 15,000 lines plus generated field code

It fits the no-C-compiler goal best and gives chibi's standard library HTTPS everywhere, including where no platform TLS exists. It is reasonable once the compiler is self-hosted and section 7.3's features exist. It should start as TLS 1.3 only, the version every host accepts, use the platform verifier on Windows and macOS, and replace platform TLS only after it passes section 7.4's suites. Zig shows the risk of skipping that step. Odin, which wrote its TLS after two years of saying the WebPKI was the obstacle, shows the time it takes.

### 8.4 The order

1. Now: run `git`, as decided on 2026-11-26.
2. chibi's git client: platform TLS, with OpenSSL 3 on Linux, and an Apple block shim until chibi builds blocks.
3. Later, if the platform paths prove unworkable somewhere: a prebuilt rustls-ffi, or Mbed TLS with the platform verifier. The owner hears about it first (the Process row of 2026-11-26).
4. After self-hosting: TLS written in chibi from Go, TLS 1.3 first, behind the platform verifier on Windows and macOS.

## 9. Unverified and open

- **CVEs for Zig's `std.crypto`.** I found none, and Zig has no security policy; the issue trackers are the only record.
- **Hare's platforms.** Hare's install page is undated.
- **Jai.** All Jai facts come from community sources. Which TLS backend its bundled libcurl uses is unverified.
- **LibreSSL CVEs.** LibreSSL keeps no CVE index; the fixes in its release notes are the only record I found.
- **BoringSSL's own bugs** go through the Chromium tracker, which I could not enumerate.
- **OpenSSL's CVE count.** I counted CVE-2026 identifiers on its vulnerabilities page; counted by publication date, the figure is higher, because some CVE-2025 entries were published in 2026.
- **My line estimates in section 7.2.** They assume a Go-style verifier. A TLS 1.3-only first version is smaller; I did not measure by how much.
- **Not measured: handshake time and binary size** of each option. TLS runs only when a dependency is not cached, so neither affects compile speed.
- **For the owner:**
  1. Platform TLS for the git client, as recommended, or one library everywhere?
  2. On Linux, the distribution's OpenSSL 3, or a vendored library?
  3. On macOS, a prebuilt block shim, or wait until chibi builds blocks?
  4. Is TLS 1.2 on Windows 10 acceptable for the git client?
