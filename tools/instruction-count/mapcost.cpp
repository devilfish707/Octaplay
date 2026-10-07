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
static uint64_t call(uint32_t fn, std::vector<uint32_t> args, uint32_t* ret) {
	uint32_t sp = SP0;
	for (auto it = args.rbegin(); it != args.rend(); ++it) { sp -= 4; M->write32(sp, *it); }
	sp -= 4; M->write32(sp, STOP);
	m68k_set_reg(M->getCpuState(), M68K_REG_A7, sp); M->setPC(fn);
	uint64_t n0 = M->instructions();
	while (M->pcFast() != STOP) if (!M->step()) return ~0ull;
	*ret = m68k_get_reg(M->getCpuState(), M68K_REG_D0);
	return M->instructions() - n0;
}
int main(int argc, char** argv) {
	std::vector<uint8_t> image(16, 0); Machine m(image); M = &m;
	std::ifstream in("blob.txt"); std::string line;
	while (std::getline(in, line)) { std::istringstream s(line); std::string a, hex; s >> a >> hex; uint32_t base = std::stoul(a, nullptr, 16); for (size_t i = 0; i < hex.size(); i += 2) m.write8(base + i / 2, std::stoul(hex.substr(i, 2), nullptr, 16)); }
	for (int i = 1; i + 2 < argc; i += 3) {
		uint32_t seed = std::stoul(argv[i]), raw = std::stoul(argv[i + 1]), r;
		uint64_t n = call(S_PM_MAP, {4, 33, 0, raw, seed}, &r);
		printf("walk %s seed %u raw %u -> step %u, %llu instructions\n", argv[i + 2], seed, raw, r, (unsigned long long)n);
	}
}
