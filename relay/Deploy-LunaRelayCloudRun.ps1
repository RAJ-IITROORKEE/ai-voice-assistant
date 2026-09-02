param(
    [string]$Region = 'us-east4',
    [string]$ServiceName = 'luna-relay',
    [string]$RepositoryName = 'luna-relay',
    [string]$ServiceAccountName = 'luna-relay-runtime',
    [int]$MinInstances = 1,
    [int]$MaxInstances = 3,
    [int]$Concurrency = 1
)

$ErrorActionPreference = 'Stop'

if ($MinInstances -lt 0 -or $MaxInstances -lt 1 -or $MinInstances -gt $MaxInstances -or $Concurrency -lt 1) {
    throw 'MinInstances must be non-negative, MaxInstances and Concurrency must be positive, and MinInstances cannot exceed MaxInstances.'
}

function Invoke-Gcloud {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & gcloud.cmd @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Google Cloud CLI command failed: gcloud $($Arguments -join ' ')"
    }
}

function Test-GcloudResource {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $previousErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & gcloud.cmd @Arguments *> $null
        return $LASTEXITCODE -eq 0
    }
    finally {
        $ErrorActionPreference = $previousErrorPreference
    }
}

function Set-RelaySecret {
    param([string]$Name, [string]$Value, [string]$RuntimeServiceAccount)

    if (-not (Test-GcloudResource secrets describe $Name --quiet)) {
        Invoke-Gcloud secrets create $Name --replication-policy automatic --quiet | Out-Null
    }
    # Reapply access on every deployment so an existing secret remains readable
    # if the runtime service account or its IAM policy was changed.
    Invoke-Gcloud secrets add-iam-policy-binding $Name --member "serviceAccount:$RuntimeServiceAccount" `
        --role roles/secretmanager.secretAccessor --quiet | Out-Null

    $temporaryPath = Join-Path ([IO.Path]::GetTempPath()) ("luna-relay-$Name-" + [guid]::NewGuid().ToString('N'))
    try {
        # Piping a PowerShell string appends a newline, which would break a token comparison.
        [IO.File]::WriteAllText($temporaryPath, $Value, [Text.UTF8Encoding]::new($false))
        & gcloud.cmd secrets versions add $Name --data-file=$temporaryPath --quiet | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Could not upload the $Name secret version."
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$settingsPath = Join-Path $root 'relay\appsettings.Local.json'
$firmwareConfigPath = Join-Path $root 'main\relay_config.h'
$statePath = Join-Path $root 'relay\.gcloud-deployment.json'
$memoryCollection = 'luna_agent_sessions'

if (-not (Test-Path -LiteralPath $settingsPath) -or -not (Test-Path -LiteralPath $firmwareConfigPath)) {
    throw 'Generate ignored local relay settings before deployment.'
}
$settings = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json
$tokenMatch = [regex]::Match(
    (Get-Content -Raw -LiteralPath $firmwareConfigPath),
    '(?m)^\s*#define\s+LUNA_RELAY_DEVICE_TOKEN\s+"(?<token>[^"]+)"\s*$')
if (-not $tokenMatch.Success) {
    throw 'Could not read the generated relay device token.'
}

Invoke-Gcloud auth list --filter=status:ACTIVE --format 'value(account)' --quiet | Out-Null
$projectId = (Invoke-Gcloud config get-value project --quiet).Trim()
if ([string]::IsNullOrWhiteSpace($projectId) -or $projectId -eq '(unset)') {
    throw 'Set an active Google Cloud project with: gcloud config set project PROJECT_ID'
}

if (Test-Path -LiteralPath $statePath) {
    $existingState = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
    if ($existingState.ProjectId -ne $projectId) {
        throw 'Existing Cloud Run deployment state belongs to a different GCP project.'
    }
    $Region = $existingState.Region
    $ServiceName = $existingState.ServiceName
    $RepositoryName = $existingState.RepositoryName
    $ServiceAccountName = ($existingState.RuntimeServiceAccount -split '@')[0]
}

Invoke-Gcloud services enable run.googleapis.com cloudbuild.googleapis.com artifactregistry.googleapis.com `
    secretmanager.googleapis.com firestore.googleapis.com --quiet | Out-Null

if (-not (Test-GcloudResource artifacts repositories describe $RepositoryName --location $Region --quiet)) {
    Invoke-Gcloud artifacts repositories create $RepositoryName --repository-format docker --location $Region --quiet | Out-Null
}

$runtimeServiceAccount = "$ServiceAccountName@$projectId.iam.gserviceaccount.com"
if (-not (Test-GcloudResource iam service-accounts describe $runtimeServiceAccount --quiet)) {
    Invoke-Gcloud iam service-accounts create $ServiceAccountName --display-name 'Luna relay runtime' --quiet | Out-Null
}

# New GCP projects can use the Compute Engine default identity for Cloud Build.
# Grant only the build and repository-write permissions it needs for this image.
$projectNumber = (Invoke-Gcloud projects describe $projectId --format 'value(projectNumber)' --quiet).Trim()
$buildServiceAccount = "$projectNumber-compute@developer.gserviceaccount.com"
Invoke-Gcloud projects add-iam-policy-binding $projectId --member "serviceAccount:$buildServiceAccount" `
    --role roles/cloudbuild.builds.builder --quiet | Out-Null
Invoke-Gcloud artifacts repositories add-iam-policy-binding $RepositoryName --location $Region `
    --member "serviceAccount:$buildServiceAccount" --role roles/artifactregistry.writer --quiet | Out-Null

Invoke-Gcloud projects add-iam-policy-binding $projectId --member "serviceAccount:$runtimeServiceAccount" `
    --role roles/datastore.user --quiet | Out-Null

if (-not (Test-GcloudResource firestore databases describe --database '(default)' --quiet)) {
    Invoke-Gcloud firestore databases create --database '(default)' --location $Region --type firestore-native --quiet | Out-Null
}
Invoke-Gcloud firestore fields ttls update expires_at --collection-group $memoryCollection `
    --database '(default)' --enable-ttl --quiet | Out-Null

Set-RelaySecret -Name 'luna-relay-device-token' -Value $tokenMatch.Groups['token'].Value -RuntimeServiceAccount $runtimeServiceAccount
Set-RelaySecret -Name 'luna-relay-speech-key' -Value $settings.AzureSpeech.Key -RuntimeServiceAccount $runtimeServiceAccount
Set-RelaySecret -Name 'luna-relay-openai-key' -Value $settings.AzureOpenAI.ApiKey -RuntimeServiceAccount $runtimeServiceAccount

$image = "$Region-docker.pkg.dev/$projectId/$RepositoryName/luna-relay:v2"
Invoke-Gcloud builds submit --config cloudbuild.yaml --substitutions "_IMAGE=$image" --quiet | Out-Null

$environment = @(
    'ASPNETCORE_ENVIRONMENT=Production',
    'Relay__ListenPort=8080',
    'Relay__UseTls=false',
    "AzureSpeech__Region=$($settings.AzureSpeech.Region)",
    "AzureSpeech__DefaultServiceVoice=$($settings.AzureSpeech.DefaultServiceVoice)",
    "AzureOpenAI__Endpoint=$($settings.AzureOpenAI.Endpoint)",
    "AzureOpenAI__Model=$($settings.AzureOpenAI.Model)",
    "Firestore__ProjectId=$projectId",
    "Firestore__ConversationCollection=$memoryCollection",
    "Assistant__DefaultProfile__RecognitionLanguage=$($settings.Assistant.DefaultProfile.RecognitionLanguage)",
    "Assistant__DefaultProfile__DefaultVoiceId=$($settings.Assistant.DefaultProfile.DefaultVoiceId)",
    'Assistant__DefaultProfile__Memory__WindowMinutes=10080',
    'Assistant__DefaultProfile__Memory__MaximumHistoryTurns=20',
    'Assistant__DefaultProfile__Memory__MaximumArchivedConversations=5',
    'Assistant__DefaultProfile__Tools__RequireConfirmationForWrites=true'
)
$secretBindings = @(
    'Relay__DeviceToken=luna-relay-device-token:latest',
    'AzureSpeech__Key=luna-relay-speech-key:latest',
    'AzureOpenAI__ApiKey=luna-relay-openai-key:latest'
)
$environmentFlag = $environment -join ','
$secretBindingsFlag = $secretBindings -join ','

 # A voice WebSocket holds provider resources for a whole turn, so each instance
 # accepts one concurrent session. Scale with bounded replica count instead.
 Invoke-Gcloud run deploy $ServiceName --image $image --region $Region --platform managed --allow-unauthenticated `
    --service-account $runtimeServiceAccount --port 8080 --min-instances $MinInstances --max-instances $MaxInstances --concurrency $Concurrency `
    --cpu 1 --memory 1Gi --no-cpu-throttling --timeout 3600 --set-env-vars $environmentFlag --set-secrets $secretBindingsFlag --quiet | Out-Null

$serviceUrl = (Invoke-Gcloud run services describe $ServiceName --region $Region --format 'value(status.url)' --quiet).Trim()
if (-not $serviceUrl.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Cloud Run did not return an HTTPS service URL.'
}
$relayUri = "wss://$($serviceUrl.Substring('https://'.Length))/voice"

$firmwareHeader = @"
#pragma once

// Generated by the Cloud Run deployment script. This file is ignored because it contains a revocable device token.
#define LUNA_RELAY_URI "$relayUri"
#define LUNA_RELAY_DEVICE_TOKEN "$($tokenMatch.Groups['token'].Value)"
#define LUNA_RELAY_USE_PUBLIC_CA 1
"@
[IO.File]::WriteAllText($firmwareConfigPath, $firmwareHeader, [Text.UTF8Encoding]::new($false))

$state = [ordered]@{
    ProjectId = $projectId
    Region = $Region
    ServiceName = $ServiceName
    RepositoryName = $RepositoryName
    RuntimeServiceAccount = $runtimeServiceAccount
    ServiceUrl = $serviceUrl
    RelayUri = $relayUri
}
[IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

Write-Host "Cloud Run relay deployed. Firmware configuration now targets $relayUri"
