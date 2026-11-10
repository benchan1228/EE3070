#pragma once
#include "esp_camera.h"
void setupProjectSensors();
void pollProjectSensors();
bool setProjectBuzzer(bool on);
void captureProjectWakeBurst();
camera_fb_t *projectCameraGet();
void projectCameraReturn(camera_fb_t *fb);
