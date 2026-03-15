import ctypes
import json
import platform
import subprocess
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = Path(__file__).resolve().parent


@pytest.fixture(scope="session")
def build_dir():
	path = TEST_DIR / "build"
	path.mkdir(parents=True, exist_ok=True)
	return path


def _run_command(cmd, *, cwd):
	subprocess.run(cmd, check=True, cwd=cwd, capture_output=True)


@pytest.fixture(scope="session")
def shared_library(build_dir):
	system = platform.system()
	lib_name = "libarena.dylib" if system == "Darwin" else "libarena.so"
	lib_path = build_dir / lib_name

	compile_cmd = [
		"gcc",
		"-std=c11",
		"-O3",
		"-fPIC",
		"-I",
		str(ROOT_DIR),
		str(ROOT_DIR / "arena.c"),
	]

	if system == "Darwin":
		compile_cmd.insert(-1, "-dynamiclib")
	else:
		compile_cmd.insert(-1, "-shared")

	compile_cmd += ["-o", str(lib_path)]

	_run_command(compile_cmd, cwd=ROOT_DIR)

	lib = ctypes.CDLL(str(lib_path))

	lib.arenaInit.restype = ctypes.c_void_p
	lib.arenaInit.argtypes = []

	lib.arenaDestroy.restype = None
	lib.arenaDestroy.argtypes = []

	lib.arenaReset.restype = ctypes.c_void_p
	lib.arenaReset.argtypes = []

	lib.arenaAlloc.restype = ctypes.c_void_p
	lib.arenaAlloc.argtypes = [ctypes.c_size_t]

	lib.arenaAllocBuffsizeBlock.restype = ctypes.c_void_p
	lib.arenaAllocBuffsizeBlock.argtypes = []

	lib.arenaIsInitialized.restype = ctypes.c_int
	lib.arenaIsInitialized.argtypes = []

	lib.arenaLocalInit.restype = ctypes.c_void_p
	lib.arenaLocalInit.argtypes = []

	lib.arenaLocalDestroy.restype = None
	lib.arenaLocalDestroy.argtypes = [ctypes.c_void_p]

	lib.arenaLocalReset.restype = ctypes.c_void_p
	lib.arenaLocalReset.argtypes = [ctypes.c_void_p]

	lib.arenaLocalAlloc.restype = ctypes.c_void_p
	lib.arenaLocalAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t]

	lib.arenaLocalAllocBuffsizeBlock.restype = ctypes.c_void_p
	lib.arenaLocalAllocBuffsizeBlock.argtypes = [ctypes.c_void_p]

	lib.arenaLocalIsInitialized.restype = ctypes.c_int
	lib.arenaLocalIsInitialized.argtypes = [ctypes.c_void_p]

	return lib


@pytest.fixture
def fresh_arena(shared_library):
	lib = shared_library
	lib.arenaDestroy()
	yield lib
	lib.arenaDestroy()


def test_global_arena_lifecycle(fresh_arena):
	lib = fresh_arena
	assert lib.arenaIsInitialized() == 0
	arena_ptr = lib.arenaInit()
	assert arena_ptr
	assert lib.arenaIsInitialized() == 1
	lib.arenaDestroy()
	assert lib.arenaIsInitialized() == 0


def test_arena_allocation_alignment(fresh_arena):
	lib = fresh_arena
	align = ctypes.alignment(ctypes.c_longdouble)

	lib.arenaInit()
	ptr1 = lib.arenaAlloc(1)
	ptr2 = lib.arenaAlloc(33)
	ptr3 = lib.arenaAlloc(128)

	for ptr in (ptr1, ptr2, ptr3):
		assert ptr != 0
		assert ptr % align == 0

	lib.arenaDestroy()


def test_arena_reset_reuses_memory(fresh_arena):
	lib = fresh_arena
	lib.arenaInit()
	first = lib.arenaAlloc(256)
	lib.arenaAlloc(512)

	lib.arenaReset()
	again = lib.arenaAlloc(256)

	assert first == again
	lib.arenaDestroy()


def test_arena_alloc_buffsize_block(fresh_arena):
	lib = fresh_arena
	lib.arenaInit()
	block = lib.arenaAllocBuffsizeBlock()
	assert block != 0
	lib.arenaDestroy()


def test_local_arena_functions(shared_library):
	lib = shared_library
	arena = lib.arenaLocalInit()
	assert arena
	assert lib.arenaLocalIsInitialized(arena) == 1

	ptr1 = lib.arenaLocalAlloc(arena, 64)
	ptr2 = lib.arenaLocalAlloc(arena, 64)
	assert ptr1 != 0 and ptr2 != 0

	lib.arenaLocalReset(arena)
	ptr3 = lib.arenaLocalAlloc(arena, 64)
	assert ptr1 == ptr3

	block = lib.arenaLocalAllocBuffsizeBlock(arena)
	assert block != 0

	lib.arenaLocalDestroy(arena)


@pytest.fixture(scope="session")
def compiled_binaries(build_dir):
	system = platform.system()

	bench_path = build_dir / "bench_stress"
	normal_path = build_dir / "normal_use_case"

	common_flags = ["-std=c11", "-O3", "-I", str(ROOT_DIR)]

	bench_cmd = ["gcc", *common_flags, str(TEST_DIR / "bench_stress.c"), str(ROOT_DIR / "arena.c"), "-o", str(bench_path)]
	normal_cmd = ["gcc", *common_flags, str(TEST_DIR / "normal_use_case.c"), str(ROOT_DIR / "arena.c"), "-o", str(normal_path)]

	if system == "Linux":
		bench_cmd += ["-pthread"]
		normal_cmd += ["-pthread"]

	_run_command(bench_cmd, cwd=ROOT_DIR)
	_run_command(normal_cmd, cwd=ROOT_DIR)

	return {"bench": bench_path, "normal": normal_path}


def test_stress_performance(compiled_binaries):
	bench_binary = compiled_binaries["bench"]
	completed = subprocess.run([str(bench_binary)], check=True, cwd=ROOT_DIR, capture_output=True, text=True)
	stdout_lines = [line for line in completed.stdout.strip().splitlines() if line.strip()]
	result = json.loads(stdout_lines[-1])

	assert result["arena_time"] < result["malloc_time"] * 1.1


def _run_normal_use_case(binary, mode):
	completed = subprocess.run(
		[str(binary), mode],
		check=True,
		cwd=ROOT_DIR,
		capture_output=True,
		text=True,
	)
	stdout_lines = [line for line in completed.stdout.strip().splitlines() if line.strip()]
	return json.loads(stdout_lines[-1])


def test_normal_use_case_performance(compiled_binaries):
	normal_binary = compiled_binaries["normal"]
	arena_result = _run_normal_use_case(normal_binary, "arena")
	malloc_result = _run_normal_use_case(normal_binary, "malloc")

	assert arena_result["checksum"] == malloc_result["checksum"]
	assert arena_result["seconds"] <= malloc_result["seconds"] * 1.5
