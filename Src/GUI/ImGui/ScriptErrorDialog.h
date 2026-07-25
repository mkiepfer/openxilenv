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


#ifndef SCRIPTERRORDIALOG_H
#define SCRIPTERRORDIALOG_H

/**
 * @class ScriptErrorDialog
 * @brief Sink for script error/info messages raised from the scheduler thread.
 * @details ABI-compatible with the Qt/Console widget of the same name. The M2
 *          skeleton forwards the message onto the GUI command queue.
 */
class ScriptErrorDialog
{
public:
    static void ScriptErrorMsgDlgAddMsgFromOtherThread(int par_Level, int par_LineNr, const char *par_Filename, const char *par_Message);
};

#endif /* SCRIPTERRORDIALOG_H */
