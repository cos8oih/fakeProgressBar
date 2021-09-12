#define NOMINMAX

#include "hackpro_ext.h"

#include <atomic>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Types

using CCRect = void;
using CCString = void;

typedef void(__thiscall* ccRectCtor_T)(
	CCRect* self,
	float const,
	float const,
	float length,
	float const);

typedef CCString* (__cdecl* createStringWithFormat_T)(
	char const* base,
	std::uint32_t const x);

// Globals

static std::string const RECT_SYM("??0CCRect@cocos2d@@QAE@MMMM@Z");
static std::string const STRING_SYM("?createWithFormat@CCString@cocos2d@@SAPAV12@PBDZZ");

static ccRectCtor_T ccRectCtor = nullptr;
static createStringWithFormat_T createStringWithFormat = nullptr;

static std::atomic<float> g_LockedLength = 0.0f;
static std::atomic<std::uint32_t> g_LockedPercentage = 0;

// Helpers

static std::string readString(std::string const& path)
{
	std::string buffer;
	std::ifstream h(path, std::ios::ate);

	if (h.is_open())
	{
		buffer.resize(h.tellg());
		h.seekg(0, std::ios::beg);

		h.read(
			buffer.data(),
			buffer.size());
	}

	return buffer;
}

static std::uint32_t parsePercentage(std::string const& s)
{
	try
	{
		return std::stoul(s);
	}
	catch (std::invalid_argument const&)
	{
		return 0u;
	}
}

static void lockPercentage(std::uint32_t const x)
{
	auto p = std::min(x, 100u);

	//This multiplier is related to the texture i think idk
	::g_LockedLength = p ? p * 2.06f : 0.0f;
	::g_LockedPercentage = p;
}

static float lockedLength() { return g_LockedLength; }
static std::uint32_t lockedPercentage() { return g_LockedPercentage; }

static void __stdcall hackproCallback(void* tbox)
{
	auto s = ::HackproGetTextBoxText(tbox);

	if (s)
		::lockPercentage(::parsePercentage(s));
}

static bool initHackPro()
{
	if (::InitialiseHackpro())
	{
		while (!::HackproIsReady());

		void* ext = ::HackproInitialiseExt("Fake PBar");
		void* tbox = ::HackproAddTextBox(ext, &::hackproCallback);
		::HackproSetTextBoxText(tbox, "0");
		::HackproSetTextBoxPlaceholder(tbox, "Percentage");
		::HackproCommitExt(ext);
		return true;
	}

	return false;
}

// Hooking

template <typename A, typename C>
static typename std::enable_if<
	std::is_integral<A>::value &&
	sizeof(A) == sizeof(std::uintptr_t) &&
	std::is_pointer<C>::value,
	bool>::type doTheHook(
	A const address,
	C const callback,
	std::size_t const size,
	bool const isCall)
{
	std::vector<std::uint8_t> buffer;

	auto const cb = reinterpret_cast<std::uintptr_t>(callback);

	if (address &&
		cb &&
		size < 5)
		return false;

	std::intptr_t const offset = cb - address - 5;

	buffer.push_back(isCall ? 0xE8 : 0xE9);

	for (auto i = 0u; i < sizeof(std::intptr_t); ++i)
		buffer.push_back(reinterpret_cast<std::uint8_t const*>(&offset)[i]);

	for (auto i = 5u; i < size; ++i)
		buffer.push_back(0x90);

	return ::WriteProcessMemory(
		::GetCurrentProcess(),
		reinterpret_cast<LPVOID>(address),
		buffer.data(),
		buffer.size(),
		nullptr) == TRUE;
}

// Callback

static void __fastcall ccRectCtorCallback(
	CCRect* self,
	void const* edx,
	float const p1,
	float const p2,
	float const length,
	float const p3)
{
	auto const l = ::lockedLength();

	::ccRectCtor(
		self,
		p1,
		p2,
		l ? std::min(length, l) : length,
		p3);
}

static CCString* __cdecl createStringCallback(
	char const* base,
	std::uint32_t const x)
{
	auto const p = ::lockedPercentage();

	return ::createStringWithFormat(
		base,
		p ? std::min(x, p) : p);
}

// Entrypoint

DWORD WINAPI MainThread(LPVOID)
{
	auto textureOffset = 0x2080CC;
	auto stringOffset = 0x208139;

	auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleA(NULL));
	auto cocos = ::GetModuleHandleA("libcocos2d.dll");

	if (base)
	{
		textureOffset += base;
		stringOffset += base;
	}

	if (cocos)
	{
		::ccRectCtor = reinterpret_cast<ccRectCtor_T>(::GetProcAddress(cocos, RECT_SYM.c_str()));

		::createStringWithFormat =
			reinterpret_cast<createStringWithFormat_T>(
				::GetProcAddress(cocos, STRING_SYM.c_str()));
	}

	auto const init = ::doTheHook(textureOffset, &::ccRectCtorCallback, 6, true) &&
		::doTheHook(stringOffset, &::createStringCallback, 6, true);

	if (!init)
	{
		::MessageBoxA(
			NULL,
			"Hooking failed!",
			"Fake progress bar",
			MB_OK | MB_ICONERROR);
		return ERROR_BAD_ENVIRONMENT;
	}

	if (!::initHackPro())
	{
		//Fallback for non mhv6 users
		::lockPercentage(
			::parsePercentage(
				::readString("percentage.txt")));
	}

	return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID const)
{
	::DisableThreadLibraryCalls(dll);

	if (reason == DLL_PROCESS_ATTACH)
		::CreateThread(0, 0, &::MainThread, 0, 0, 0);

	return TRUE;
}