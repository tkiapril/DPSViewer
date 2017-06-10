#include "MemoryScanner.h"
#include "Tools.h"
#include <Psapi.h>


MemoryScanner::MemoryScanner() {
	DWORD cbNeeded;
	EnumProcessModulesEx(GetCurrentProcess(), &hGame, sizeof(hGame), &cbNeeded, LIST_MODULES_32BIT | LIST_MODULES_64BIT);

	peHeader = (IMAGE_NT_HEADERS*) ((DWORD_PTR) hGame + (DWORD_PTR) ((PIMAGE_DOS_HEADER) hGame)->e_lfanew);
	sectionHeaders = (IMAGE_SECTION_HEADER*) &(peHeader[1]);
	for (int i = 0; i < peHeader->FileHeader.NumberOfSections; i++, sectionHeaders++)
		if (0 == strncmp((char*) sectionHeaders->Name, ".text", 5))
			goto textSectionFound;
	return;
textSectionFound:
	mScannerThread = CreateThread(NULL, NULL, MemoryScanner::DoScanExternal, this, NULL, NULL);

	TCHAR ffxivPath[MAX_PATH];
	GetModuleFileNameEx(GetCurrentProcess(), NULL, ffxivPath, MAX_PATH);
	bool kor = wcsstr(ffxivPath, L"KOREA") ? true : false;

#ifdef _WIN64
	// GLO v3.57, KOR v3.4
	ActorMap.push_back(DataSignature("48c1e8033dffff0000742b3da80100007324488d0d", 21, true, 2, new int[2]{ 0, 0 }));
	TargetMap.push_back(DataSignature("400fb6f684c0410f45f5488d0d", 13, true, 2, new int[2]{ 0, 184 }));
	if (kor) {
		Struct.Actor = { 116, 48, 132, 138, 5336 };
		Struct.Target = { 0, 32, 80 };
	} else {
		Struct.Actor = { 116, 48, 132, 140, 5184 };
		Struct.Target = { 0, 32, 80 };
	}
#else
	// GLO v3.57, KOR v3.4
	ActorMap.push_back(DataSignature("81feffff0000743581fe58010000732d8b3cb5", 19, false, 2, new int[2]{ 0, 0 }));
	TargetMap.push_back(DataSignature("750e85d2750ab9", 7, false, 2, new int[2]{ 0, 108 }));
	PartyMap.push_back(DataSignature("85c074178b407450b9", 9, false, 2, new int[2]{ 0, 16 }));
	if (kor) {
		Struct.Actor = { 116, 48, 132, 138, 4656 };
		Struct.Target = { 0, 16, 48 };
	} else {
		Struct.Actor = { 116, 48, 132, 140, 4504 };
		Struct.Target = { 0, 16, 48 };
	}
#endif
}


MemoryScanner::~MemoryScanner() {
}

MemoryScanner::DataSignature::DataSignature(std::string signature, int offset, bool rip, int pathcount, int *path) :
	offset(offset),
	rip(rip) {
	for (auto it = signature.begin(); it != signature.end(); it += 2) {
		char t[3] = { *it, *(&*it + 1), 0 };
		if (t[0] == '*') {
			this->signature.push_back(0);
			this->mask.push_back('?');
		} else {
			this->signature.push_back((char) strtol(t, nullptr, 16));
			this->mask.push_back('x');
		}
	}
	if (pathcount > 0) {
		for (int i = 0; i < pathcount; i++)
			this->path.push_back(path[i]);
		delete path;
	}

}

bool MemoryScanner::ScanInto(PVOID *p, DataSignature &s) {
	__try {
		char *ptr = (char*) Tools::FindPattern((DWORD_PTR) hGame, peHeader->OptionalHeader.SizeOfImage, (BYTE*) s.signature.c_str(), (char*) s.mask.c_str());
		if (!ptr) return false;
		ptr += s.offset;
		char *next = ptr;
		for (int i = 0; i < s.path.size(); i++) {
			ptr = next + s.path[i];
			if (i + 1 < s.path.size()) {
				if (i == 0 && s.rip)
					next = ptr + 4 + *(int*) ptr;
				else
					next = *(char**) ptr;
			}
		}
		*p = ptr;
		return true;
	} __except (1) {
		return false;
	}
}

void MemoryScanner::ScanInto(PVOID *p, std::vector<DataSignature> &map) {
	*p = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		if (ScanInto(p, *it))
			break;
	}
}

DWORD MemoryScanner::DoScan() {

#ifdef _WIN64
	Result.Functions.ProcessWindowMessage = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x40\x53\x48\x83\xEC\x60\x48\x89\x6C\x24\x70\x48\x8D\x4C\x24\x30\x33\xED\x45\x33\xC9\x45\x33\xC0\x33\xD2\x89\x6C\x24\x20\xBB\x0A", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
	Result.Functions.ProcessNewLine = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x48\x89\x5C\x24\x20\x55\x56\x57\x48\x81\xEC\x10\x01\x00\x00\x48\x8B\x05\x00\x00\x00\x01\x48\x33\xC4\x48\x89\x84\x24\x00\x01\x00\x00\x41\x8B\x00\x41\x8B\xE9\x49\x8B\x00\x83\xE0\x0F\x48\x8B\xDA", "xxxxxxxxxxxxxxxxxx???xxxxxxxxxxxxxxxxxxxx?xxxxxx");
	Result.Functions.HideFFXIVWindow = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x40\x55\x48\x83\xEC\x20\x48\x8B\x91\xC8\x00\x00\x00\x48\x8B\xE9\x48\x85\xD2", "xxxxxxxxxxxxxxxxxxx");
	Result.Functions.ShowFFXIVWindow = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x40\x53\x48\x83\xEC\x40\x48\x8B\x91\xC8\x00\x00\x00\x48\x8B\xD9\x48\x85\xD2", "xxxxxxxxxxxxxxxxxxx");
#else
	Result.Functions.ProcessWindowMessage = (PVOID) Tools::FindPattern((DWORD) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x55\x8B\xEC\x83\xEC\x1C\x53\x8B\x1D\x00\x00\x00\x00\x57\x6A\x00", "xxxxxxxxx????xxx");
	Result.Functions.ProcessNewLine = (PVOID) (hGame + sectionHeaders->VirtualAddress - 1);
	do {
		Result.Functions.ProcessNewLine = (PVOID) Tools::FindPattern((DWORD) Result.Functions.ProcessNewLine + 1,
			sectionHeaders->Misc.VirtualSize - (DWORD) Result.Functions.ProcessNewLine + (DWORD) (hGame + sectionHeaders->VirtualAddress),
			(PBYTE) "\x55\x8B\xEC\x81\xEC\xAC\x00\x00\x00\xA1\x00\x00\x00\x00\x33\xC5\x89\x45\xFC\x53\x8B\x5D", "xxxxxxxxxx????xxxxxxxx");
		if (Result.Functions.ProcessNewLine && 0 != Tools::FindPattern((DWORD) Result.Functions.ProcessNewLine, 0x300,
			(PBYTE) "\xFF\x83\x7D\x10\x02\x7F\x16\x8D\x85\x54\xFF\xFF\xFF\x50\x8D\x4D\xA8\x51\x8D",
			"xxxxxxxxxxxxxxxxxxx"))
			break;
	} while (Result.Functions.ProcessNewLine && (int) (sectionHeaders->Misc.VirtualSize - (DWORD) Result.Functions.ProcessNewLine + (DWORD) (hGame + sectionHeaders->VirtualAddress)) > 0);
	Result.Functions.HideFFXIVWindow = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x56\x8B\xF1\x8B\x86\x80\x00\x00\x00\x85\xC0\x0F\x00\x00\x00\x00\x00\x8B\x8E\x00\x00\x00\x00\xC1\xE9\x07\xF6\xC1\x01\x75\x7a", "xxxxxxxxxxxx?????xx????xxxxxxxx");
	Result.Functions.ShowFFXIVWindow = (PVOID) Tools::FindPattern((DWORD_PTR) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x56\x8B\xF1\x8B\x86\x80\x00\x00\x00\x85\xC0\x0F\x00\x00\x00\x00\x00\x8B\x8E\x00\x00\x00\x00\xC1\xE9\x07\xF6\xC1\x01\x0f\x85", "xxxxxxxxxxxx?????xx????xxxxxxxx");
#endif

	ScanInto(&Result.Data.ActorMap, ActorMap);
	ScanInto(&Result.Data.TargetMap, TargetMap);
	ScanInto(&Result.Data.PartyMap, PartyMap);


	CloseHandle(mScannerThread);
	mScannerThread = INVALID_HANDLE_VALUE;
	return 0;
}

bool MemoryScanner::IsScanComplete() {
	return mScannerThread == INVALID_HANDLE_VALUE;
}