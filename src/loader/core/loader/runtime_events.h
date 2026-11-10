#pragma once

#include "mod_sdk.h"

#include <cstdint>

struct Il2CppApi;

void RuntimeEvents_Reset();
void RuntimeEvents_StopWorkers();
uint64_t RuntimeEvents_ConfigureIl2Cpp(Il2CppApi &il2cpp);
uint64_t RuntimeEvents_Capabilities();
void RuntimeEvents_AfterModsLoaded();
void RuntimeEvents_Pump();
int RuntimeEvents_CurrentScene(URK_SceneInfo *scene);
// Scene reports for backends without Unity hooks; distinct forces a reload of the same map.
void RuntimeEvents_ObserveScene(const URK_SceneInfo &scene, bool distinct);
void RuntimeEvents_SetMainThread(unsigned long threadId);

int RuntimeEvents_MenuCursorSetOpen(void *ownerModule, int open);
int RuntimeEvents_MenuMouseCaptureSet(void *ownerModule, int capture);
int RuntimeEvents_UnregisterModule(void *module);
int RuntimeEvents_CursorStateGet(URK_CursorState *state);
int RuntimeEvents_CursorStateSet(const URK_CursorState *state);
int RuntimeEvents_InputGetKey(int32_t keyCode);
int RuntimeEvents_InputGetKeyDown(int32_t keyCode);
int RuntimeEvents_InputGetKeyUp(int32_t keyCode);
int RuntimeEvents_InputGetMouseButton(int32_t button);
int RuntimeEvents_InputGetMouseButtonDown(int32_t button);
int RuntimeEvents_InputGetMouseButtonUp(int32_t button);
int32_t RuntimeEvents_GraphicsDeviceType();
int RuntimeEvents_IsMainThread();



