/*
 * Copyright 2023 ZF Friedrichshafen AG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef ERRORDIALOG_H
#define ERRORDIALOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void InitErrorDialog(void);
void TerminateErrorDialog(void);
void CheckErrorMessagesBeforeMainWindowIsOpen(void);

/* Called by the (non-NO_GUI) ThrowError.c border file to route an error to the
 * GUI. The M2 skeleton has no modal windows yet, so this prints the message and
 * returns IDOK (mirrors the NO_GUI console path). */
int ErrorMessagesFromOtherThreadInst_Error(int par_Level, uint64_t par_Cycle, const char *par_HeaderString, const char *par_ErrorString, int par_IsGuiMainThreadFlag);
int IsMainThread(void);

#ifdef __cplusplus
}
#endif

#endif /* ERRORDIALOG_H */
