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

/* Called from the scheduler thread by A2LLink.c to notify A2L calibration
 * widgets that a process link changed. No such widgets exist in the M2 skeleton,
 * so this is a no-op stub. */
int GlobalNotifiyA2LCalMapWidgetFuncSchedThread(const char *par_ProcessName, int par_Flags, int par_Action);
int GlobalNotifiyGetDataFromLinkAckCallback(void *par_IndexData, int par_FetchDataChannelNo, void *par_Param);

#ifdef __cplusplus
}
#endif

#endif /* A2LCALWIDGETSYNC_H */
