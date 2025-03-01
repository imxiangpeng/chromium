// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_ENCRYPTION_MIGRATION_MODE_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_ENCRYPTION_MIGRATION_MODE_H_

namespace chromeos {

// Defines the mode of the encryption migration screen.
enum class EncryptionMigrationMode {
  // Ask the user if migration should be started.
  ASK_USER,
  // Start migration immediately.
  START_MIGRATION,
  // Resume incomplete migration.
  RESUME_MIGRATION
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_ENCRYPTION_MIGRATION_MODE_H_
