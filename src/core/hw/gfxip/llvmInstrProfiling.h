/*===- InstrProfiling.h- Support library for PGO instrumentation ----------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_H_
#define PROFILE_INSTRPROFILING_H_

/*!
 * \brief Clear profile counters to zero.
 *
 */
void __llvm_profile_reset_counters(void);

/*!
 * \brief Write instrumentation data to the current file.
 *
 * Writes to the file with the last name given to \a *
 * __llvm_profile_set_filename(),
 * or if it hasn't been called, the \c LLVM_PROFILE_FILE environment variable,
 * or if that's not set, the last name set to INSTR_PROF_PROFILE_NAME_VAR,
 * or if that's not set,  \c "default.profraw".
 */
int __llvm_profile_write_file(void);

int __llvm_orderfile_write_file(void);
/*!
 * \brief this is a wrapper interface to \c __llvm_profile_write_file.
 * After this interface is invoked, a arleady dumped flag will be set
 * so that profile won't be dumped again during program exit.
 * Invocation of interface __llvm_profile_reset_counters will clear
 * the flag. This interface is designed to be used to collect profile
 * data from user selected hot regions. The use model is
 *      __llvm_profile_reset_counters();
 *      ... hot region 1
 *      __llvm_profile_dump();
 *      .. some other code
 *      __llvm_profile_reset_counters();
 *       ... hot region 2
 *      __llvm_profile_dump();
 *
 *  It is expected that on-line profile merging is on with \c %m specifier
 *  used in profile filename . If merging is  not turned on, user is expected
 *  to invoke __llvm_profile_set_filename  to specify different profile names
 *  for different regions before dumping to avoid profile write clobbering.
 */
int __llvm_profile_dump(void);

int __llvm_orderfile_dump(void);

/*!
 * \brief Set the filename for writing instrumentation data.
 *
 * Sets the filename to be used for subsequent calls to
 * \a __llvm_profile_write_file().
 *
 * \c Name is not copied, so it must remain valid.  Passing NULL resets the
 * filename logic to the default behaviour.
 */
void __llvm_profile_set_filename(const char *Name);

/*! \brief Register to write instrumentation data to file at exit. */
int __llvm_profile_register_write_file_atexit(void);

/*! \brief Initialize file handling. */
void __llvm_profile_initialize_file(void);

/*!
 * \brief Return path prefix (excluding the base filename) of the profile data.
 * This is useful for users using \c -fprofile-generate=./path_prefix who do
 * not care about the default raw profile name. It is also useful to collect
 * more than more profile data files dumped in the same directory (Online
 * merge mode is turned on for instrumented programs with shared libs).
 * Side-effect: this API call will invoke malloc with dynamic memory allocation.
 */
const char *__llvm_profile_get_path_prefix();

/*!
 * \brief Return filename (including path) of the profile data. Note that if the
 * user calls __llvm_profile_set_filename later after invoking this interface,
 * the actual file name may differ from what is returned here.
 * Side-effect: this API call will invoke malloc with dynamic memory allocation.
 */
const char *__llvm_profile_get_filename();

/*! \brief Get the magic token for the file format. */
uint64_t __llvm_profile_get_magic(void);

/*! \brief Get the version of the file format. */
uint64_t __llvm_profile_get_version(void);

#endif /* PROFILE_INSTRPROFILING_H_ */
