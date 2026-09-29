FROM mcr.microsoft.com/dotnet/sdk:8.0 AS build
WORKDIR /src

COPY relay/Directory.Build.props relay/
COPY relay/src/LunaRelay.Protocol/LunaRelay.Protocol.csproj relay/src/LunaRelay.Protocol/
COPY relay/src/LunaRelay/LunaRelay.csproj relay/src/LunaRelay/
RUN dotnet restore relay/src/LunaRelay/LunaRelay.csproj

COPY relay/src/LunaRelay.Protocol/ relay/src/LunaRelay.Protocol/
COPY relay/src/LunaRelay/ relay/src/LunaRelay/
# Restore in the same layer as publish so the NuGet package cache is consistent
# (splitting restore/publish across layers can drop Microsoft.Extensions.Logging.Abstractions).
RUN dotnet publish relay/src/LunaRelay/LunaRelay.csproj --configuration Release --output /out /p:UseAppHost=false

FROM mcr.microsoft.com/dotnet/aspnet:8.0
WORKDIR /app
COPY --from=build /out/ .
ENV ASPNETCORE_URLS=http://+:8080
ENV ASPNETCORE_ENVIRONMENT=Production
EXPOSE 8080
ENTRYPOINT ["dotnet", "LunaRelay.dll"]
