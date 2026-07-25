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


#ifndef MAINWINOWSYNCWITHOTHERTHREADS_H
#define MAINWINOWSYNCWITHOTHERTHREADS_H

#include <stdint.h>

/*
 * GUI/core seam ABI shim for the ImGui front-end.
 *
 * This mirrors the C-linkage declarations of the canonical Qt seam header
 * (Src/GUI/Qt/MainWinowSyncWithOtherThreads.h) exactly. The C core calls these
 * *FromOtherThread() functions; the ImGui implementation
 * (ImGuiSyncWithOtherThreads.cpp) packages each call onto a thread-safe command
 * queue that the render thread drains once per frame.
 */

#ifdef __cplusplus
extern "C" {
#endif

void SchedulerStateChanged(int par_State);
void SchedulerStateChangedAck(int par_ThreadId);

int OpenWindowByNameFromOtherThread(char *par_WindowName, int par_PosFlag, int par_XPos, int par_YPos, int par_SizeFlag, int par_Width, int par_Hight);
void OpenWindowByNameFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int IsWindowOpenFromOtherThread(char *par_WindowName);
void IsWindowOpenFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
void SaveAllConfigToIniDataBaseFromOtherThread(void);
void SaveAllConfigToIniDataBaseFromOtherThreadAck(int par_ThreadId);
int CloseWindowByNameFromOtherThread(char *par_WindowName);
void CloseWindowByNameFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);

int SelectSheetFromOtherThread(char *par_SheetName);
void SelectSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int AddSheetFromOtherThread(char *par_SheetName);
void AddSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int DeleteSheetFromOtherThread(char *par_SheetName);
void DeleteSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int RenameSheetFromOtherThread(char *par_OldSheetName, char *par_NewSheetName);
void RenameSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);

int ImportHotkeyFromOtherThread(char *par_Filename);
void ImportHotkeyFromOtherThreadAck(int par_ThreadId, int par_ReturnValue);
int ExportHotkeyFromOtherThread(char *par_Filename);
void ExportHotkeyFromOtherThreadAck(int par_ThreadId, int par_ReturnValue);

void SetNotFasterThanRealtimeState(int par_State);
void SetSuppressDisplayNonExistValuesState(int par_State);
void DeactivateBreakpointFromOtherThread(void);

void AddNewScriptErrorMessageFromOtherThread(int par_Level, int par_LineNr, const char *par_Filename, const char *par_Message);
void AddNewScriptErrorMessageFromOtherThreadAck(int par_ThreadId);

int OpenProgressBarFromOtherThread(const char *par_ProgressName);
void SetProgressBarFromOtherThread(int par_ProgressBarID, int par_Value);
void CloseProgressBarFromOtherThread(int par_ProgressBarID);

/* Script commands: CREATE_DLG/ADD_DLG_ITEM/SHOW_DLG */
int ScriptCreateDialogFromOtherThread(char *par_Headline);
void ScriptCreateDialogFromOtherThreadAck(int par_ThreadId, int par_ReturnValue);
int ScriptAddDialogItemFromOtherThread(char *par_Element, char *par_Label);
void ScriptAddDialogItemFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int ScriptShowDialogFromOtherThread(void);
void ScriptShowDialogFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int ScriptDestroyDialogFromOtherThread(void);
void ScriptDestroyDialogFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);
int IsScriptDialogClosedFromOtherThread(void);
void IsScriptDialogClosedFromOtherThreadAck(int par_ThreadId, int par_RetrunValue);

void StopOscilloscopeFromOtherThread(void *par_OscilloscopeWidget, uint64_t par_Time);

/* Socket based remote API */
int RemoteProcedureCallRequestFromOtherThread(void *par_Connection, const void *par_In, void *ret_Out);
void RemoteProcedureCallRequestFromOtherThreaddAck(int par_ThreadId, int par_RetrunValue);

/*
 * Handshake flag telling the error/message layer that the GUI thread is up and
 * able to service *FromOtherThread requests (behavioural drop-in for the Qt
 * ErrorDialog helper of the same name).
 */
void SetSyncWithGuiThreadIsInitAndRunning(void);
int GetSyncWithGuiThreadIsInitAndRunning(void);

/*
 * Drains all queued GUI commands on the render (main) thread and wakes any
 * blocked caller via its *Ack(). Call once per frame.
 */
void DrainGuiCommandQueueFromOtherThread(void);

#ifdef __cplusplus
}
#endif

#endif /* MAINWINOWSYNCWITHOTHERTHREADS_H */
