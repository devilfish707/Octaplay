// Instruction counts for the Play Modes unit (built code only, no firmware):
// the unit's linked .text/.rodata are loaded into an otherwise empty machine,
// the firmware bytes it reads are set to synthetic values, and each entry is
// called with a return address the loop stops at.
#include "machine.h"
#include "mc68k/Musashi/m68k.h"
#include "mc68k/cpuState.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "syms.h"
using namespace ot;
static const uint32_t STOP = 0x46f00000, SP0 = 0x46e00000;
static Machine* M;
static uint32_t minSp = 0xffffffff;
static void w8(uint32_t a, uint8_t v) { M->write8(a, v); }
static void w32(uint32_t a, uint32_t v) { M->write32(a, v); }
static uint64_t call(uint32_t fn, std::vector<uint32_t> args) {
	uint32_t sp = SP0;
	for (auto it = args.rbegin(); it != args.rend(); ++it) { sp -= 4; w32(sp, *it); }
	sp -= 4; w32(sp, STOP);
	m68k_set_reg(M->getCpuState(), M68K_REG_A7, sp);
	M->setPC(fn);
	uint64_t n0 = M->instructions(), guard = 0;
	while (M->pcFast() != STOP) { if (!M->step()) { printf("step failed at %08x: %s\n", M->pc(), M->why().c_str()); return ~0ull; } { uint32_t a7 = m68k_get_reg(M->getCpuState(), M68K_REG_A7); if (a7 < minSp) minSp = a7; } if (++guard > 50000000) { printf("runaway\n"); return ~0ull; } }
	return M->instructions() - n0;
}
enum : uint32_t { BLOB = 0x400e21e0, BANKS = 0x9b340, PAT = 0x8ed8, SEQ = 0x80006500 };
static void pattern(unsigned bank, unsigned p, unsigned perTrack, unsigned len, unsigned master) {
	uint32_t b = BLOB + bank * BANKS + p * PAT;
	w8(b + 0x8e53, len); w8(b + 0x8e54, 2); w8(b + 0x8e55, perTrack);
	w8(b + 0x8e50, master >> 8); w8(b + 0x8e51, master & 255);
	for (unsigned t = 0; t < 8; ++t) { w8(b + t * 0x91a + 0x50, len); w8(b + t * 0x91a + 0x51, t % 7); }
	for (unsigned t = 0; t < 8; ++t) { w8(b + 0x48d0 + t * 0x8b0 + 0x28, len); w8(b + 0x48d0 + t * 0x8b0 + 0x29, t % 7); }
}
int main() {
	std::vector<uint8_t> image(16, 0);	// no firmware: an empty machine
	Machine m(image); M = &m;
	std::ifstream in("blob.txt"); std::string line;
	while (std::getline(in, line)) { std::istringstream s(line); std::string a, hex; s >> a >> hex; uint32_t base = std::stoul(a, nullptr, 16); for (size_t i = 0; i < hex.size(); i += 2) w8(base + i / 2, std::stoul(hex.substr(i, 2), nullptr, 16)); }
	w32(SEQ + 0xb8, 1);	// transport: playing
	w8(SEQ + 0xbd, 0); w8(SEQ + 0xbe, 0);
	uint64_t first = call(S_PM_STEP_ENTRY, {0, 0});
	printf("first use after boot (battery table load): %llu\n", (unsigned long long)first);
	uint64_t worst = 0, worstShow = 0, worstPeek = 0; unsigned wl = 0, wm = 0;
	for (unsigned mode = 0; mode < 6; ++mode)
	for (unsigned per = 0; per < 2; ++per)
	for (unsigned len : {1u, 2u, 3u, 16u, 17u, 31u, 33u, 47u, 63u, 64u}) {
		pattern(0, 0, per, len, per ? 0xffff : 0);
		for (unsigned i = 0; i < 17; ++i) w8(S_PM_TABLE + i, mode);	// row A01: every slot this mode
		m.write16(S_PM_CUR, 0);											// re-read the row
		for (unsigned run = 0; run < 3; ++run) {
			w32(S_PM_RESTART, 1);	// a transport start since the last step
			for (unsigned pass = 0; pass < 4; ++pass)
			for (unsigned raw = 0; raw < len; ++raw)
			for (unsigned t = 0; t < 16; ++t) {
				uint64_t n = call(S_PM_STEP_ENTRY, {t, raw});
				if (n > worst) { worst = n; wl = len; wm = mode; }
				uint64_t s = call(S_PM_SHOW_ENTRY, {t, raw}); if (s > worstShow) worstShow = s;
				uint64_t k = call(S_PM_PEEK_ENTRY, {t, raw + 1}); if (k > worstPeek) worstPeek = k;
			}
		}
	}
	// MASTER LENGTH 16 over 14-step tracks: the master-restart path, every mode
	{
		uint64_t wm16 = 0;
		for (unsigned mode = 0; mode < 6; ++mode) {
			pattern(0, 0, 1, 14, 16);
			for (unsigned i = 0; i < 17; ++i) w8(S_PM_TABLE + i, mode);
			m.write16(S_PM_CUR, 0); w32(S_PM_RESTART, 1);
			for (unsigned loop = 0; loop < 6; ++loop) for (unsigned i = 0; i < 16; ++i) for (unsigned t = 0; t < 16; ++t) {
				uint64_t n = call(S_PM_STEP_ENTRY, {t, i < 14 ? i : i - 14}); if (n > wm16) wm16 = n; }
		}
		printf("master-restart case worst one-track step: %llu\n", (unsigned long long)wm16);
	}
	// per mode, and SHUFFLE over many more runs (seeds) at every length
	for (unsigned mode = 0; mode < 6; ++mode) {
		uint64_t wmode = 0;
		for (unsigned len = 1; len <= 64; ++len) {
			pattern(0, 0, 1, len, 0xffff);
			for (unsigned i = 0; i < 17; ++i) w8(S_PM_TABLE + i, mode);
			m.write16(S_PM_CUR, 0);
			unsigned runs = mode == 4 ? 60 : 2;
			for (unsigned run = 0; run < runs; ++run) {
				w32(S_PM_RESTART, 1);
				for (unsigned raw = 0; raw < len; ++raw) for (unsigned t = 0; t < 16; t += (mode == 4 ? 1 : 5)) {
					uint64_t n = call(S_PM_STEP_ENTRY, {t, raw}); if (n > wmode) wmode = n;
				}
			}
		}
		printf("mode %u worst one-track step: %llu\n", mode, (unsigned long long)wmode);
		if (wmode > worst) { worst = wmode; wm = mode; }
	}
	printf("worst pm_step_entry (one track step): %llu instructions (len %u, mode %u)\n", (unsigned long long)worst, wl, wm);
	printf("worst pm_show_entry (UI step query): %llu\n", (unsigned long long)worstShow);
	printf("worst pm_peek_entry (rebuild/look-ahead): %llu\n", (unsigned long long)worstPeek);
	// a step on which every track restarts: 16 calls, the first with the restart
	pattern(0, 0, 1, 33, 0xffff);
	for (unsigned i = 0; i < 17; ++i) w8(S_PM_TABLE + i, 4);
	m.write16(S_PM_CUR, 0);
	uint64_t worstStep = 0;
	for (unsigned raw = 0; raw < 33; ++raw) { w32(S_PM_RESTART, 1); uint64_t sum = 0; for (unsigned t = 0; t < 16; ++t) sum += call(S_PM_STEP_ENTRY, {t, raw}); if (sum > worstStep) worstStep = sum; }
	printf("worst 16-track step with restart (SHUFFLE, len 33): %llu\n", (unsigned long long)worstStep);
	// UI events (key handler / project load / pattern copy context)
	{
		uint64_t key = 0; for (int i = 0; i < 12; ++i) { uint64_t n = call(S_PM_KEY_UPDOWN, {(uint32_t)(i % 16), (uint32_t)(i < 6 ? 1 : -1)}); if (n > key) key = n; }
		printf("pm_key_updown (mode change, battery row): %llu\n", (unsigned long long)key);
		printf("pm_project_begin(storing): %llu\n", (unsigned long long)call(S_PM_PROJECT_BEGIN, {1}));
		const char* l = "#PLAY_MODES=P16:54321054321054321";
		for (unsigned i = 0; l[i - (i ? 1 : 0)] || !i; ++i) w8(0x46d00000 + i, l[i]);
		printf("pm_project_line (one line): %llu\n", (unsigned long long)call(S_PM_PROJECT_LINE, {0x46d00000, 0}));
		const char* l2 = "#PLAY_MODES=12345012345012345";
		for (unsigned i = 0; i <= 29; ++i) w8(0x46d00100 + i, l2[i]);
		printf("pm_project_line (legacy single line, every pattern): %llu\n", (unsigned long long)call(S_PM_PROJECT_LINE, {0x46d00100, 0}));
		uint64_t fmt = 0; for (unsigned i = 0; i < 256; ++i) fmt += call(S_PM_PROJECT_FORMAT, {S_PM_LINE, i});
		printf("pm_project_format x256 (a project write, all patterns set): %llu\n", (unsigned long long)fmt);
		printf("pm_pattern_copy (paste): %llu\n", (unsigned long long)call(S_PM_PATTERN_COPY, {BLOB + PAT, 0x460c8122u, PAT}));
		printf("pm_pattern_clear: %llu\n", (unsigned long long)call(S_PM_PATTERN_CLEAR, {BLOB + PAT}));
	}
	printf("deepest stack below the caller's arguments: %u bytes\n", (unsigned)(SP0 - 4 - minSp));
	return 0;
}
