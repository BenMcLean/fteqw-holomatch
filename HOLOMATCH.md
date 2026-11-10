# Holomatch Support for FTEQW: Goals, Findings, Progress

Living document. Update as work proceeds.

## Goals

1. Add support for *Star Trek Voyager: Elite Force Holomatch* (EF multiplayer) to FTEQW's Quake 3 module (`plugins/quake3/`).
2. Keep all changes behind compile-time flags so upstream FTEQW contributions can still be merged.
3. **GPL only.** Merge GPL code into GPL code and fix it up. Do not use or depend on Raven's proprietary EF/Holomatch SDK (game, cgame, ui source). Holomatch game logic is loaded from the player's own retail `.pk3`/QVM files, as for any Q3 mod.

## Reference projects (all GPLv2 engines)

| Project | Notes |
|---|---|
| [thiloschulz/ioef](https://github.com/thiloschulz/ioef) | Original ioEF (2005-2016), the base of the other two. Last commit is a 2016 ioq3 merge with no function tests. |
| [clover-moe/lilium-voyager](https://github.com/clover-moe/lilium-voyager) | Maintained ioEF successor. **Primary reference.** |
| [Chomenor/ioef-cmod](https://github.com/Chomenor/ioef-cmod) | ioEF plus a large feature layer. Cherry-pick source only. |

All three express the EF delta as `#ifdef ELITEFORCE` blocks in engine code. Their `code/cgame`, `code/game` and `code/ui` directories are stock id Q3 code. Lilium's README says the Raven game source is not included.

## Findings

### Lilium vs cMod

- Same core EF engine delta (netcode, syscalls, structs).
- **Lilium adds:** player origin rounding compatible with retail QVMs (prediction), a protocol 24 download fix, ioEF 1.37 network compatibility, official Raven master listing, Opus VoIP, optional Flexible Display (widescreen).
- **cMod adds:** a new filesystem (11.6k lines), aspect correction and HUD options, map brightness adjust (`cmod_map_adjust.c`, 278 lines), asset validation (3.9k lines), crosshair/cvar/logging systems (about 21k lines of crosshair data), MP3 codec, and `vm_extensions.c` (extra syscalls for cMod's own QVMs).
- **Decision:** use Lilium as the primary reference. From cMod, consider only map brightness adjust and possibly asset checks later. Skip the filesystem (FTE has its own VFS), `vm_extensions` (not for retail QVMs) and the other UI/cvar extras.
- **Open:** cMod has 387 `ELITEFORCE` refs vs Lilium's 345. Diff the two to find any compatibility fixes one has and the other lacks.

### EF engine delta (from ioEF, about 306 refs, 63 files)

- Protocol numbers 24 and 26 (stock Q3 uses 68 and 71). `msg_t` gets a `compat` flag for old servers.
- `playerState_t`: adds `rechargeTime`, `useTime`, `introTime`, `damageShieldCount`. `MAX_PS_EVENTS` is 4. Drops `grapplePoint`, `generic1`, `loopSound` and the jumppad fields.
- `usercmd_t` has a different layout, with its own `MSG_*DeltaUsercmd`. Plus an extra `ACTION_ALTATTACK` and different `svFlags` values (`SVF_SHIELD_BBOX`, `SVF_ELIMINATED`).
- cgame, ui and game syscall tables differ from Q3 (see "Batch mapping, 3/4/6" for the exact differences; the cgame table is mostly additive, the ui and game tables shift).
- Renderer: `RT_*` entity types (`RT_ALPHAVERTPOLY`, `RT_BEAM`, `RT_LINE` etc.), `RF_FULLBRIGHT`, `RF_FORCE_ENT_ALPHA`, `RegisterShader3D`, no deform-text strings in `refdef_t`.
- Bot API: `EA_AltAttack`, item and inventory use/drop, a different `BotSetChatName` signature.
- Naming and config: base game `baseEF`, heartbeat `STEF1`, config `hmconfig.cfg`, master port 27953.

### FTEQW side

- Q3 support is a plugin in `plugins/quake3/` (about 10.6k lines: `clq3_cg.c`, `clq3_ui.c`, `clq3_parse.c`, `svq3_game.c`, `q3common.c`, `q3g_public.h`). It is **not** a port of ioq3's `code/client`.
- That means the ioEF `ELITEFORCE` blocks are a **spec to re-implement**, not patches to apply.
- Enabled by `Q3CLIENT` / `Q3SERVER` in `engine/common/config_*.h`.
- **Unverified:** whether netcode and renderer changes fit inside the plugin or need hooks in FTE core.

### Feasibility verdicts (read-only pass)

**Wiring.** `plugins/quake3/` is not a runtime plugin. The Makefile and CMake build it into the engine, and `Q3CLIENT`/`Q3SERVER` hooks run through `engine/client`, `engine/server` and `engine/common`. Compile flags can therefore cover both plugin files and the few core hooks.

**Batch 2, netcode: feasible and self-contained.** FTE keeps the Q3 delta tables (`MSG_Q3_ReadDeltaEntity`, `MSGQ3_WriteDeltaEntity`, `*DeltaPlayerstate`, `*DeltaUsercmd`) and `Netchan_*Q3` in `plugins/quake3/q3common.c`. ioEF's EF changes in `msg.c` are:
- byte-aligned strings and data in "compat" mode (protocol 24 servers)
- no high-ASCII or `%` sanitising
- 8-bit `buttons` in `usercmd`
- an EF `entityStateFields` table
- predefined 32-entry change-vector encoding for entity deltas (compat mode)

All of these map to `q3common.c`, though the msg read/write path will need a compat-mode flag threaded through. The `q3playerState_t`, `q3entityState_t` and `q3usercmd_t` structs in `clq3defs.h` need an EF variant (batch 1).

**Batch 5, renderer: feasible, and smaller than feared, but all new code.** The cgame-to-renderer boundary is `VQ3_AddEntity` in `clq3_ui.c`, which converts a Q3 `refEntity` into FTE's own `entity_t`. FTE's renderer is never touched by Q3-specific code. EF's renderer delta (from Lilium `renderergl1`) is:
- `RegisterShader3D` (LIGHTMAP_NONE variant of shader registration)
- `depthfunc disable` shader keyword
- `RF_FULLBRIGHT` and `RF_FORCE_ENT_ALPHA` (override shader alpha with the entity alpha)
- a different environment-map texcoord formula
- an original-fastsky colour option
- no deform-text strings in `refdef_t`
- a `refEntity` layout with a union for sprite, beam and line data
- new entity types: `RT_ORIENTEDSPRITE`, `RT_ALPHAVERTPOLY`, `RT_LINE`, `RT_ORIENTEDLINE`, `RT_LINE2`, `RT_BEZIER`, `RT_CYLINDER`, `RT_ELECTRICITY`

The new entity types are the main work. They are custom primitives that ioEF draws inside its renderer. In FTE they would be built CPU-side as polygons through `scenefuncs->AddPolydata`, the same way `VQ3_AddPolys` works. The shader keyword, alpha override and fullbright need small hooks in FTE's shader and lighting code, which would be the only core-renderer touches. No verdict yet on how each of the bezier, cylinder and electricity primitives should look. That needs reading ioEF's `tr_surface.c` closely.

**Overall:** feasible. The risks are volume and the retail-QVM unknowns. Nothing found so far needs deep FTE core changes.

### Batch mapping, 3/4/6 (read-only; ioEF/Lilium blocks to FTE files)

**Key scoping insight: two EF wire protocols.** ioEF's `msg->compat` / `clc.compat` / `client->compat` flag is the original Raven protocol 24 (EF 1.2). It has out-of-band-style messages, no Huffman, no checksum-feed key on usercmds, byte-aligned strings, `svc` ends with `-1` instead of `svc_EOF`, and no `clientNum` / checksumFeed in gamestate in some paths. Protocol 26 is ioEF's own variant, much closer to ioq3 netcode. Most of the `compat` branches in `msg.c`, `cl_parse.c`, `sv_client.c`, `sv_snapshot.c` and `cl_input.c` exist for protocol 24 only.
**Proposal:** implement protocol 26 first, and treat protocol 24 compatibility as a later, separate batch (2b). *Not yet verified:* which protocol current Holomatch servers (e.g. cMod/holomat.ch) actually require, and whether usercmd key-xoring is dropped for both protocols (ioEF's `sv_client.c` removes the key under `ELITEFORCE` without a `compat` check).

**Batch 3, syscall tables.** FTE implements each VM interface as one big `switch` on a Q3 enum in `clq3_cg.c`, `clq3_ui.c` and `svq3_game.c`. The clean approach is a separate EF enum plus a translation table (EF trap number to existing Q3 case), with new `case`s only for EF-only traps. That keeps the existing Q3 code untouched.
- *cgame imports* (`clq3_cg.c`): **mostly additive.** Indices 0 through `CG_MEMORY_REMAINING` match Q3, so the existing table works. EF adds `CG_R_REGISTERSHADER3D` (59) and `CG_CVAR_SET_NO_MODIFY` (60). The math traps at 100+ are the same minus `CG_ACOS`. EF drops the Q3 additions after `MEMORY_REMAINING` (fonts, key catcher, PC_* parser, real time, snapvector, cinematics, capsule, `CG_FS_SEEK`, etc.). Also, `FS_SEEK`, capsule and additive-light cases must be refused for EF rather than silently honoured.
- *cgame exports:* EF has no `CG_KEY_EVENT`, `CG_MOUSE_EVENT` or `CG_EVENT_HANDLING`. FTE must not call them, and must not use `KEYCATCH_CGAME` for EF.
- *ui imports* (`clq3_ui.c`): **shifts.** EF inserts four `UI_LAN_GET{LOCAL,GLOBAL}SERVER{COUNT,ADDRESSSTRING}` traps after `UI_GETCONFIGSTRING`, which renumbers everything after. It then ends at `UI_SET_CDKEY`, `UI_R_MODELBOUNDS`, with no `GET_CDKEY`, fonts, PC_*, real time, cinematics or cached servers. `UI_KEY_EVENT` takes one argument (no up/down flag). `UI_API_VERSION` is 6. The cd-key traps are stubs (ioEF just returns `qtrue` and sets `ui_cdkeychecked2`).
- *game imports* (`svq3_game.c`, `q3g_public.h`): the first part is the same as Q3 through `G_DEBUG_POLYGON_DELETE`. EF has no `G_REAL_TIME`, `G_SNAPVECTOR`, capsule or `G_FS_SEEK`, and no `G_MEMSET`-style math traps (in the 100 block). Its botlib section uses a **different, older layout** (the AAS 300 block and the `EA_*` 400 block, including `USE_ITEM`/`DROP_ITEM`/`USE_INV`/`DROP_INV`/`ALT_ATTACK`). `BOTLIB_AI_LOAD_CHARACTER` takes an int instead of a float, and `BOTLIB_AI_SET_CHAT_NAME` has a different signature. Whether Holomatch actually uses bots is unverified; if not, the whole botlib block can be a stub.

**Batch 4, client/server glue** (ioEF files to FTE files):

| ioEF | What EF does | FTE location |
|---|---|---|
| `cl_main.c`, `sv_client.c`, `cl_parse.c` | connect handshake, gamestate and download quirks, `disconnect` handling, `err_dialog` | `plugins/quake3/clq3_parse.c` (`CLQ3_SendConnectPacket`, `CLQ3_ParseGameState`, `CLQ3_ParseDownload`), `svq3_game.c` |
| `cl_main.c`, `sv_main.c` | master server and getservers parsing (different byte-encoded response format), `infoResponse` quoting, heartbeat `STEF1`, port 27953, `getstatus`, `efmaster` hosts | `engine/client/net_master.c` (many `Q3CLIENT` hooks already), `engine/server/sv_main.c` |
| `cl_main.c` | `.efdemo` extension, `charsgrid_med` charset shader, `cl_maxpackets 43` | `engine/client/cl_main.c`, `clq3_*.c` |
| `cl_input.c`, `cl_keys.c` | `+altattack`, `+use` buttons (buttons 5 and 6), UI key event without up/down flag, no cgame key catcher | `engine/client/cl_input.c`, `clq3_ui.c` |
| `files.c`, `common.c`, `q_shared.h` | `baseEF` basegame, `hmconfig.cfg`, retail pak checksums, "Raven pk3" download refusal, zone size 32MB, `efq3.key` cd key | `engine/common/fs.c` (game definition table: add an `-ef` entry beside `-quake3` at line 189; note `Q3CFG` sets `com_protocolversion 68`) |
| `sv_init.c` | `sv_fps` as a SYSTEMINFO cvar, master hostnames | `engine/server/sv_init.c` |

**Batch 6, leftovers:**
- `snd_codec.c`: `S_MangleNameEF` and the `sound/voice` dir (EF voice sample name mangling), probably in the engine sound loader.
- `cl_scrn.c`, `cl_console.c`: EF console and chat drawing (small font, `say_team:` prompt).
- `sv_snapshot.c`: drops the Q3 `generic1` handling.
- `be_ai_chat.c`, `be_ea.c`, `botlib.h`: bot API (only if bots are in scope).
- `snd_openal.c`, `snd_dma.c`, `tr_init.c`: small tweaks, to be read when we get there.

### Decisions

1. **Protocol 24 is out of scope.** Backwards compatibility is required for game data, not for the wire protocol. Protocol 24 is Raven's legacy EF 1.2 protocol and only matters for joining servers or clients that still speak it. Drop all `compat` branches. This batch (2b) is cancelled, not deferred.
   *Tradeoff measured:* Lilium has about 180 `compat`/legacy-protocol references across 18 files (`cl_main.c` 48, `msg.c` 32, `sv_client.c` 26, `net_chan.c` 13, `sv_net_chan.c` 10, `cl_parse.c` 9, and others). The compat path changes the bit reader/writer (byte-aligned strings), entity delta encoding (32 predefined change vectors), the netchan, message framing in both directions (no Huffman, `-1` as end marker, different gamestate layout), and demo handling. Verifying it needs a real protocol 24 peer. ioEF retrofitted it as scattered `if (compat)` branches, whereas FTE's Q3 netcode is separate functions, so it could be added later as its own code path. *What it would buy:* joining pure retail EF 1.2 servers/clients (ioEF, cMod and Lilium servers all accept protocol 26) and playing old `.efdemo` files (protocol 24 format; the one item near the "keep game data" requirement). *Revisit only if* there is real demand for retail servers or old demos (a demo-only reader could be done separately).
2. **Target protocol 26 exactly** (ioEF's native protocol), instead of inventing a new wire format. The EF `entityState`/`playerState` tables are needed either way, so matching ioEF costs about the same and keeps interop with ioEF, cMod and Lilium servers. *Unverified:* the protocol live holomat.ch servers actually run (expected 26). Confirm before relying on interop.
3. **Bots are deferred** (wanted eventually). They are cleanly separable: only the game-side `BOTLIB_*` traps (200 to 500 blocks) and `be_*`. Core work only needs those traps to be safe stubs that report bots unavailable, so the retail game QVM doesn't crash. The real EF botlib layout (`EA_*` additions, `BotLoadCharacter` and `BotSetChatName` signature changes) comes later, as its own batch.

## Plan: batches

Each batch is its own set of commits behind a compile flag, ending in a testable milestone.

| # | Batch | ioEF source | Milestone |
|---|---|---|---|
| 1 | Data layout (`playerState_t`, `usercmd_t`, `entityState_t`, `svFlags`) | `q_shared.h`, `g_public.h` | Structs match retail QVMs. |
| 2 | Netcode (delta tables, protocol 26 only; no protocol 24/compat) | `msg.c`, `net_chan.c`, `sv_snapshot.c`, `cl_parse.c`, `*_net_chan.c` | An EF demo or server snapshot parses. |
| 3 | Syscall tables (cgame, ui, game) | `*_public.h`, `cl_cgame.c`, `cl_ui.c`, `sv_game.c` | Retail `ui.qvm` loads and the menu draws. |
| 4 | Client/server glue (connect, input, init, fs names) | `cl_main.c`, `sv_client.c`, `sv_init.c`, `cl_input.c`, `files.c` | Client connects to a local dedicated server. |
| 5 | Renderer (`RT_*`, `RF_*`, `RegisterShader3D`, shader quirks) | `tr_*` | Holomatch maps and models render. |
| 6 | Leftovers (sound name mangling, console/screen) | `snd_*`, `cl_scrn.c`, `cl_console.c` | Polish. |
| 7 (deferred) | Real EF bots (botlib layout, `EA_*`, signature changes); batch 3 stubs the traps until then | `be_*`, `botlib.h` | Bots play a match. |

## Risks / open questions

1. **Batch 5:** the new `RT_*` primitives (line, oriented line, line2, bezier, cylinder, electricity) must be re-implemented as CPU-built polygons. Their exact geometry needs a close read of ioEF's `tr_surface.c`.
2. Core hooks are limited to the shader keyword, alpha override and fullbright. Everything else lives in `plugins/quake3/`. Confirm when we reach the code.
3. Which of Lilium vs cMod has compatibility fixes the other lacks (diff the `ELITEFORCE` blocks)?
4. Provenance audit: when copying engine code, make sure no Raven SDK comments or prose come along. Structural facts (struct layouts, trap numbers) are interface facts, not SDK text.

## Progress

- [x] Cloned and surveyed ioEF, cMod and Lilium (read-only, in a scratch directory)
- [x] Confirmed the GPL-engine / no-SDK scope and that the reference repos contain no Raven source
- [x] Chose Lilium as primary reference (see Findings)
- [x] Read-only feasibility pass: batches 2 and 5 judged feasible (see "Feasibility verdicts")
- [x] Batches 3, 4 and 6 mapped to FTE files (see "Batch mapping, 3/4/6")
- [x] Decided: protocol 24 dropped, protocol 26 targeted, bots deferred (stubbed in core work)
- [ ] Confirm live holomat.ch servers run protocol 26
- [ ] Diff Lilium vs cMod `ELITEFORCE` blocks
- [x] Batch 1: struct layouts behind `HOLOMATCH` (`clq3defs.h`, `q3g_public.h`, `svq3_game.c`)
- [x] Batch 2: EF entity/playerstate field tables, key-less 8-bit-button usercmd, `Q3_NOENCRYPT` forced on (`q3common.c`, `clq3_parse.c`, `q3common.h`)
- [x] Batch 3: cgame (`REGISTERSHADER3D`, `CVAR_SET_NO_MODIFY`, no key export, empty-name music stop), ui (4 LAN traps backed by the combined server list, EF numbering, key-down only), game (see Batch 7)
- [x] Batch 4: `-holomatch` game entry (baseEF, protocol 26), server accepts protocol 26, `+altattack`/`+use` as buttons 5/6, `hmconfig.cfg`, `sv_fps` in systeminfo, EF master hosts (port 27953), EF hex-encoded `getservers` responses (auto-detected), `heartbeat STEF1`, `getservers` uses protocol 26, `err_dialog` after a server disconnect. Deliberately skipped: `.efdemo` (protocol 24), `cl_maxpackets 43` (FTE's q3 client sends one packet per frame), `charsgrid_med` (FTE draws its own console font), retail pak download refusal (FTE's pak handling differs).
- [x] Batch 5: EF refEntity layout, `RT_ORIENTEDSPRITE`/`LINE`/`ORIENTEDLINE`/`LINE2`/`BEZIER`/`CYLINDER`/`ELECTRICITY` as cpu-built polygons (`plugins/quake3/efrender.h`), `ALPHAVERTPOLY` as a sprite, `RF_FULLBRIGHT`, `RF_FORCE_ENT_ALPHA` (mapped to the engine's forced transparency when the entity alpha is below 255), `depthfunc disable`, `RegisterShader3D` (non-2d shader registration), EF environment-map texcoord formula in the GL, Vulkan and D3D8/9/11 backends. Original fastsky colour: set `r_fastskycolour "0.8 0.7 0.4"`. Limits: camera-facing primitives use the previous frame's view origin; the env-map change also applies to the glsl-less cpu path only and affects every game in a `HOLOMATCH` build.
- [x] Batch 6: German voice mangling (`engine/client/snd_mem.c`). Nothing else needed: console/chat drawing is FTE's own, and missing sounds are already silent.
- [x] Batch 7 (bots): EF botlib trap numbers are translated onto the q3 botlib (`plugins/quake3/efbotlib.h`, generated by name from Lilium's enum); EF-only traps (`USE_ITEM`, `DROP_ITEM`, `USE_INV`, `DROP_INV`, `ALT_ATTACK`) are handled, `ACTION_ALTATTACK` added, `EA_Respawn` presses attack, `BotLoadCharacter` takes an int skill, chat speaker comes from `BotEnterChat`. The three never-implemented AAS visibility traps return 0. Retail EF ships no `.aas` files, so bots need user-supplied AAS data to do anything.

**Tested against the retail game (GOG install, `BaseEF` paks):** a `HOLOMATCH` dedicated server loads the retail `qagame.qvm` and botlib (retail `hm_voy1.aas` loads), and a `HOLOMATCH` GL client loads the retail `ui.qvm` and `cgame.qvm`, connects over protocol 26, parses snapshots with the EF delta tables, and renders the Holomatch map, HUD and viewmodel. Not yet exercised: moving/shooting, other players' models and animations, bots in play, voice/sound, other maps. Issues found and fixed during that testing: `UI_CVAR_CREATE`/`UI_R_ADDPOLYTOSCENE` were unimplemented; EF's `glconfig_t` differs from Q3's (smaller extensions string plus an anisotropy field), which zeroed the UI/cgame screen scale and gave a black screen; EF player models are Raven MDR (`RDM5`), loaded by the new `engine/common/com_mdr.inc` (frames are baked to per-frame vertices, tags to md3-style tags). Retail needs `ui_cdkeychecked 1` (the retail `hmconfig.cfg` sets it, and is imported on startup).

**Building:** everything is behind `-DHOLOMATCH` (add it to the CFLAGS for both the engine and the quake3 plugin). On Windows with msys2 (ucrt64): `make sv-rel gl-rel FTE_TARGET=win64 CC=gcc PRECOMPHEADERS= CFLAGS="-pipe -DHOLOMATCH -w -IC:/tools/msys64/ucrt64/include/opus" TMP=C:	mp TEMP=C:	mp` from `engine/`. You need the msys2 packages libvorbis, speex, libpng, libjpeg-turbo and opus, and their DLLs (libjpeg-8, libpng16-16, zlib1, libvorbis*, libogg, libopus, libspeex*, libwinpthread) next to the exe. gcc needs `-pipe` (or a Windows-style `TMP`) because msys `sh` drops `TMP`.

## Attribution

Code copied from ioEF, cMod or Lilium keeps its original copyright headers (Thilo Schulz, Zack Middleton, Noah Metzger, id Software, ioquake3 contributors) and GPLv2 terms.
