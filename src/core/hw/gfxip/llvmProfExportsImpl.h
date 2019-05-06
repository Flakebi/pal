#include "util/palMutex.h"

extern "C" {
enum ValueKind {
	IPVK_IndirectCallTarget = 0,
	IPVK_MemOPSize = 1,
	IPVK_First = IPVK_IndirectCallTarget,
	IPVK_Last = IPVK_MemOPSize,
};

typedef struct {
	const uint64_t NameRef;
	const uint64_t FuncHash;
	const void* CounterPtr;
	/* This is used to map function pointers for the indirect call targets to
	 * function name hashes during the conversion from raw to merged profile
	 * data.
	 */
	const void* FunctionPointer;
	void* Values;
	const uint32_t NumCounters;
	const uint16_t NumValueSites[IPVK_Last+1];
} __llvm_profile_data;

typedef struct ValueProfNode {
	uint64_t Value;
	uint64_t Count;
	struct ValueProfNode* Next;
} ValueProfNode;

Util::Mutex LlvmProfileMutex;

static const __llvm_profile_data *DataFirst = nullptr;
static const __llvm_profile_data *DataLast = nullptr;
static const char *NamesFirst = nullptr;
static const char *NamesLast = nullptr;
static uint64_t *CountersFirst = nullptr;
static uint64_t *CountersLast = nullptr;
static uint32_t *OrderFileFirst = nullptr;
// Mark as IR instrumentation
uint64_t __llvm_profile_raw_version = 4 | (0x1ULL << 56);

const __llvm_profile_data *__llvm_profile_begin_data(void) { return DataFirst; }
const __llvm_profile_data *__llvm_profile_end_data(void) { return DataLast; }
const char *__llvm_profile_begin_names(void) { return NamesFirst; }
const char *__llvm_profile_end_names(void) { return NamesLast; }
uint64_t *__llvm_profile_begin_counters(void) { return CountersFirst; }
uint64_t *__llvm_profile_end_counters(void) { return CountersLast; }
uint32_t *__llvm_profile_begin_orderfile(void) { return OrderFileFirst; }

ValueProfNode *__llvm_profile_begin_vnodes(void) { return 0; }
ValueProfNode *__llvm_profile_end_vnodes(void) { return 0; }

ValueProfNode *CurrentVNode = 0;
ValueProfNode *EndVNode = 0;
} // End extern "C"
