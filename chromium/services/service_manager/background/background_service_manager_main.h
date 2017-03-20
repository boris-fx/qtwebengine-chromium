// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_SERVICE_MANAGER_BACKGROUND_BACKGROUND_SERVICE_MANAGER_MAIN_H_
#define SERVICES_SERVICE_MANAGER_BACKGROUND_BACKGROUND_SERVICE_MANAGER_MAIN_H_

// The "main" gn target supplies a file with a main() that calls to the child
// process as necessary. For the main process this function is called.
int MasterProcessMain(int argc, char** argv);

#endif  // SERVICES_SERVICE_MANAGER_BACKGROUND_BACKGROUND_SERVICE_MANAGER_MAIN_H_
