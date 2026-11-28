# Bedrock biome accuracy smoke test.
# Run from repository root after building slime_bedrock_biome_query.exe.

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot '..\..\build\slime_bedrock_biome_query.exe'
if (-not (Test-Path $exe)) { throw "missing $exe" }
$fail = 0

function Query([string]$seed, [string]$version, [int]$x, [int]$z, [int]$y) {
    $line = & $exe $seed $version $x $z $y 2>$null
    if ($LASTEXITCODE -ne 0) { throw "query failed: $line" }
    $parts = ($line -split ' ')
    return @{ id = [int]$parts[6].Split('=')[1]; name = $parts[7].Split('=')[1] }
}

function Check([string]$name, $actual, $expected) {
    if ($actual -ne $expected) {
        Write-Host ("FAIL {0}: got {1}, expected {2}" -f $name, $actual, $expected) -ForegroundColor Red
        $script:fail++
    } else {
        Write-Host ("OK   {0}: {1}" -f $name, $actual)
    }
}

# Known independent sample from MCPE-161245 (river biome, modern Bedrock).
$known = Query '2229010591266423951' '1.21.60' -2984 4033 74
Check 'MCPE-161245 river sample' $known.name 'river'

# Version-dependent behavior: same column is not river in 1.16.
$old = Query '2229010591266423951' '1.16' -2984 4033 74
Check '1.16 version difference' $old.name 'birch_forest'

# Basic known samples.
Check 'seed0 modern river' (Query '0' '1.21.60' 0 0 63).name 'river'
Check 'seed0 modern plains' (Query '0' '1.21.60' 104 104 63).name 'plains'
Check 'seed0 1.16 swamp' (Query '0' '1.16' 0 0 63).name 'swamp'

# Batch output must match single-point output.
$batch = @('8 8 63', '104 104 63') | & $exe --batch 0 1.21.60
if ($LASTEXITCODE -ne 0 -or $batch.Count -ne 2) { Check 'batch output count' $batch.Count 2 }
$batchNames = @($batch | ForEach-Object { ($_ -split ' ')[-1].Trim() })
Check 'batch first' $batchNames[0] 'river'
Check 'batch second' $batchNames[1] 'plains'

# Repeatability: same query three times.
$r1 = Query '0' '1.21.60' 0 0 63
$r2 = Query '0' '1.21.60' 0 0 63
$r3 = Query '0' '1.21.60' 0 0 63
Check 'repeatability' (($r1.name -eq $r2.name) -and ($r2.name -eq $r3.name)) $true

if ($fail -ne 0) {
    Write-Host "BEDROCK ACCURACY FAILED: $fail" -ForegroundColor Red
    exit 1
}
Write-Host 'BEDROCK ACCURACY OK' -ForegroundColor Green
