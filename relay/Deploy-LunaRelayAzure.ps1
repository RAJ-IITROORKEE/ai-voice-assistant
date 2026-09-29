#Requires -Version 7.0
<#
.SYNOPSIS
  Deploys the Luna relay to Azure Container Apps.

.DESCRIPTION
  Production deployment for the Luna Voice Assistant relay on Azure Container Apps.
  Conversation memory stays in GCP Firestore and is reached from Azure through
  Workload Identity Federation (no service-account keys). All secrets are read
  from the git-ignored relay/appsettings.Local.json and are never committed.

  Idempotent: existing resources are reused; only the image and revision change.

.PARAMETER Tag
  Container image tag to build and deploy (defaults to an incrementing 'vN' is
  left to the caller; pass an explicit tag for traceability).

.EXAMPLE
  ./relay/Deploy-LunaRelayAzure.ps1 -Tag v6
#>
[CmdletBinding()]
param(
    [string]$ResourceGroup = 'luna-relay-rg',
    [string]$Location = 'centralindia',
    [string]$AcrName = 'lunarelayacr',
    [string]$AppName = 'luna-relay',
    # Azure Container Apps allows only one environment per region per subscription.
    # Reuse the existing environment that hosts this region.
    [string]$EnvironmentName = 'qr-relay-env',
    [string]$EnvironmentResourceGroup = 'qr-relay-rg',
    [string]$IdentityName = 'luna-relay-identity',
    [string]$ImageName = 'luna-relay',
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$MinReplicas = 1,
    [int]$MaxReplicas = 3
)

$ErrorActionPreference = 'Stop'

function Invoke-Az { param([Parameter(ValueFromRemainingArguments)][string[]]$A)
    & az @A
    if ($LASTEXITCODE -ne 0) { throw "az $($A -join ' ') failed." }
}
function Invoke-Gcloud { param([Parameter(ValueFromRemainingArguments)][string[]]$A)
    & gcloud.cmd @A
    if ($LASTEXITCODE -ne 0) { throw "gcloud $($A -join ' ') failed." }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$settingsPath = Join-Path $root 'relay\appsettings.Local.json'
$firmwareConfigPath = Join-Path $root 'main\relay_config.h'
$statePath = Join-Path $root 'relay\.azure-deployment.json'
$memoryCollection = 'luna_agent_sessions'

if (-not (Test-Path -LiteralPath $settingsPath)) { throw 'Missing relay/appsettings.Local.json (git-ignored secrets).' }
$settings = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json
if (-not (Test-Path -LiteralPath $firmwareConfigPath)) { throw 'Missing main/relay_config.h.' }
$tokenMatch = [regex]::Match((Get-Content -Raw -LiteralPath $firmwareConfigPath),
    '(?m)^\s*#define\s+LUNA_RELAY_DEVICE_TOKEN\s+"(?<token>[^"]+)"\s*$')
if (-not $tokenMatch.Success) { throw 'Could not read LUNA_RELAY_DEVICE_TOKEN.' }

$subscriptionId = (Invoke-Az account show --query id -o tsv).Trim()
$tenantId = (Invoke-Az account show --query tenantId -o tsv).Trim()

# --- Azure core resources -----------------------------------------------------
if (-not (az group show --name $ResourceGroup 2>$null)) { Invoke-Az group create --name $ResourceGroup --location $Location | Out-Null }
if (-not (az acr show --name $AcrName --resource-group $ResourceGroup 2>$null)) {
    Invoke-Az acr create --resource-group $ResourceGroup --name $AcrName --sku Basic | Out-Null
}
$acrLogin = (Invoke-Az acr show --name $AcrName --resource-group $ResourceGroup --query loginServer -o tsv).Trim()

if (-not (az identity show --name $IdentityName --resource-group $ResourceGroup 2>$null)) {
    Invoke-Az identity create --name $IdentityName --resource-group $ResourceGroup --location $Location | Out-Null
}
$identity = Invoke-Az identity show --name $IdentityName --resource-group $ResourceGroup | ConvertFrom-Json
$identityId = $identity.id
$identityClientId = $identity.clientId
$identityPrincipalId = $identity.principalId

$acrId = (Invoke-Az acr show --name $AcrName --resource-group $ResourceGroup --query id -o tsv).Trim()
Invoke-Az role assignment create --assignee-object-id $identityPrincipalId --assignee-principal-type ServicePrincipal `
    --role AcrPull --scope $acrId 2>$null | Out-Null

$environmentId = "/subscriptions/$subscriptionId/resourceGroups/$EnvironmentResourceGroup/providers/Microsoft.App/managedEnvironments/$EnvironmentName"

# --- GCP Workload Identity Federation (memory bridge, key-less) --------------
$gcpProject = (Invoke-Gcloud config get-value project --quiet).Trim()
$gcpProjectNumber = (Invoke-Gcloud projects describe $gcpProject --format 'value(projectNumber)' --quiet).Trim()
$pool = 'luna-azure-pool'
$provider = 'azure-provider'
$runtimeSa = "luna-relay-runtime@$gcpProject.iam.gserviceaccount.com"
# Entra ID app registered as the WIF audience (see AGENTS.md). Must match AgentMemory.cs azureWifAudience.
$wifAppId = 'b0ac73f1-8f1d-42f5-af7a-c0af3c3aa54f'
$wifAppAudience = "api://$wifAppId"
# Container Apps managed identity issues v1 tokens whose issuer is sts.windows.net.
$issuer = "https://sts.windows.net/$tenantId/"

if (-not (gcloud.cmd iam workload-identity-pools describe $pool --location=global 2>$null)) {
    Invoke-Gcloud iam workload-identity-pools create $pool --location=global --display-name 'Luna Azure Pool' --quiet | Out-Null
}
if (-not (gcloud.cmd iam workload-identity-pools providers describe $provider --location=global --workload-identity-pool=$pool 2>$null)) {
    Invoke-Gcloud iam workload-identity-pools providers create-oidc $provider --location=global `
        --workload-identity-pool=$pool --issuer-uri=$issuer --allowed-audiences=$wifAppAudience `
        --attribute-mapping='google.subject=assertion.sub' --display-name 'Azure AD provider' --quiet | Out-Null
}
$wifMember = "principal://iam.googleapis.com/projects/$gcpProjectNumber/locations/global/workloadIdentityPools/$pool/subject/$identityPrincipalId"
Invoke-Gcloud iam service-accounts add-iam-policy-binding $runtimeSa --role roles/iam.workloadIdentityUser `
    --member $wifMember --quiet | Out-Null

# --- Build + push image (remote; no local Docker required) --------------------
$image = "$acrLogin/$ImageName`:$Tag"
Invoke-Az acr build --registry $AcrName --image "$ImageName`:$Tag" --file relay/Dockerfile $root | Out-Null

# --- Container App ------------------------------------------------------------
$envVars = @(
    'ASPNETCORE_ENVIRONMENT=Production',
    'Relay__ListenPort=8080',
    'Relay__UseTls=false',
    "AzureSpeech__Region=$($settings.AzureSpeech.Region)",
    "AzureSpeech__DefaultServiceVoice=$($settings.AzureSpeech.DefaultServiceVoice)",
    "AzureOpenAI__Endpoint=$($settings.AzureOpenAI.Endpoint)",
    "AzureOpenAI__Model=$($settings.AzureOpenAI.Model)",
    "Firestore__ProjectId=$gcpProject",
    "Firestore__ConversationCollection=$memoryCollection",
    "Firestore__AzureClientId=$identityClientId",
    "Firestore__GcpServiceAccount=$runtimeSa",
    "Firestore__WorkloadIdentityPool=$pool",
    "Firestore__WorkloadIdentityProvider=$provider",
    "Firestore__GcpProjectNumber=$gcpProjectNumber",
    "Assistant__DefaultProfile__RecognitionLanguage=$($settings.Assistant.DefaultProfile.RecognitionLanguage)",
    "Assistant__DefaultProfile__DefaultVoiceId=$($settings.Assistant.DefaultProfile.DefaultVoiceId)",
    'Assistant__DefaultProfile__Memory__WindowMinutes=10080',
    'Assistant__DefaultProfile__Memory__MaximumHistoryTurns=20',
    'Assistant__DefaultProfile__Memory__MaximumArchivedConversations=5',
    'Assistant__DefaultProfile__Tools__RequireConfirmationForWrites=true'
)
$secretEnv = @(
    'Relay__DeviceToken=secretref:relay-device-token',
    'AzureSpeech__Key=secretref:speech-key',
    'AzureOpenAI__ApiKey=secretref:openai-key'
)

if (az containerapp show --name $AppName --resource-group $ResourceGroup 2>$null) {
    Invoke-Az containerapp update --name $AppName --resource-group $ResourceGroup --image $image | Out-Null
} else {
    Invoke-Az containerapp create --name $AppName --resource-group $ResourceGroup --environment $environmentId `
        --image $image --registry-server $acrLogin --user-assigned $identityId --registry-identity $identityId `
        --target-port 8080 --ingress external --transport auto --min-replicas $MinReplicas --max-replicas $MaxReplicas `
        --cpu 1.0 --memory 2Gi `
        --secrets relay-device-token=$($tokenMatch.Groups['token'].Value) speech-key=$($settings.AzureSpeech.Key) openai-key=$($settings.AzureOpenAI.ApiKey) `
        --env-vars $envVars $secretEnv | Out-Null
}

$fqdn = (Invoke-Az containerapp show --name $AppName --resource-group $ResourceGroup --query properties.configuration.ingress.fqdn -o tsv).Trim()
$relayUri = "wss://$fqdn/voice"

$firmwareHeader = @"
#pragma once

// Generated by the Azure Container Apps deployment script. This file is ignored because it contains a revocable device token.
#define LUNA_RELAY_URI "$relayUri"
#define LUNA_RELAY_DEVICE_TOKEN "$($tokenMatch.Groups['token'].Value)"
#define LUNA_RELAY_USE_PUBLIC_CA 1
"@
[IO.File]::WriteAllText($firmwareConfigPath, $firmwareHeader, [Text.UTF8Encoding]::new($false))

$state = [ordered]@{
    ResourceGroup = $ResourceGroup; Location = $Location; AcrName = $AcrName; AppName = $AppName
    EnvironmentName = $EnvironmentName; IdentityName = $IdentityName; Image = $image; Tag = $Tag
    Fqdn = $fqdn; RelayUri = $relayUri; GcpProject = $gcpProject; WifPool = $pool; WifProvider = $provider
}
[IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

Write-Host "Azure relay deployed. Firmware configuration now targets $relayUri"
