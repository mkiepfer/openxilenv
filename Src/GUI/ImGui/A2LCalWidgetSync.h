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


#ifndef A2LCALWIDGETSYNC_H
#define A2LCALWIDGETSYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* C-ABI entry points called from the A2L link/scheduler thread. Defined by the
 * thread-marshalling bridge in Framework/A2LCalBridge.cpp, which enqueues each
 * event for dispatch on the ImGui render thread. Signatures are fixed by the C core. */
int GlobalNotifiyA2LCalMapWidgetFuncSchedThread(const char *par_ProcessName, int par_Flags, int par_Action);
int GlobalNotifiyGetDataFromLinkAckCallback(void *par_IndexData, int par_FetchDataChannelNo, void *par_Param);

#ifdef __cplusplus
}
#endif

#endif /* A2LCALWIDGETSYNC_H */
