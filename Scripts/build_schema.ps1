# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# Rebuilds Backend/supabase/schema.sql from the ordered migrations in
# Backend/supabase/migrations. The migrations are the source of truth; the
# schema file is only a convenience for setting up a new database in one go.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\build_schema.ps1

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Migrations = Join-Path $Root "Backend\supabase\migrations"
$Out = Join-Path $Root "Backend\supabase\schema.sql"

$header = @"
-- CS-Fusion database schema - GENERATED, do not edit by hand.
--
-- This file is every migration in Backend/supabase/migrations, in order
-- (Scripts/build_schema.ps1). For a NEW database run it once in the Supabase
-- SQL editor. For an existing database apply the migrations it has not had
-- yet, in order - each one is safe to re-run.
--
-- Then run Backend/supabase/tests/security_tests.sql to check the result.

"@

$parts = @($header)
Get-ChildItem $Migrations -Filter "*.sql" | Sort-Object Name | ForEach-Object {
    $parts += "-- =============================================================================="
    $parts += "-- $($_.Name)"
    $parts += "-- =============================================================================="
    $parts += [System.IO.File]::ReadAllText($_.FullName).TrimEnd()
    $parts += ""
}
[System.IO.File]::WriteAllText($Out, ($parts -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "schema.sql rebuilt from $((Get-ChildItem $Migrations -Filter '*.sql').Count) migration(s)."
