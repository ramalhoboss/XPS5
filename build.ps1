# Builds XPS5's payloads on Windows: build\XPS5.elf (the installer, with the
# title and the notifications payload in it) and build\XPS5-uninstall.elf.
# Needs:
#   - LLVM/clang (winget install LLVM.LLVM), by default in C:\Program Files\LLVM
#   - the prebuilt ps5-payload-sdk (-Sdk)
#   - for the title itself (-Wsl): WSL with clang 18 and PS5_Vulkan in
#     ~/src/PS5_Vulkan, see title\build-title.sh
#
# Use: .\build.ps1 [-Url https://x.com/notifications] [-Sdk <ps5-payload-sdk>] [-Llvm <LLVM\bin>]
#                  [-Wsl Ubuntu-24.04]

param(
    [string]$Url = "https://x.com/home",
    [string]$Sdk = "$PSScriptRoot\..\BrowserPS5\third_party\ps5-payload-sdk-bin\ps5-payload-sdk",
    [string]$Llvm = "C:\Program Files\LLVM\bin",
    [string]$Wsl = ""
)

$ErrorActionPreference = "Stop"

$cc = Join-Path $Sdk "win\prospero-clang.cmd"
if (-not (Test-Path $cc)) { throw "SDK nao encontrado: $cc" }
if (-not (Test-Path (Join-Path $Llvm "clang.exe"))) { throw "clang nao encontrado em $Llvm" }
if ($Url -notmatch '^https?://[^\s"\\]+$') { throw "URL invalido: $Url" }
$env:PATH = "$Llvm;$env:PATH"

# O URL vai num header gerado: passar aspas por PowerShell -> .cmd -> clang parte-se.
$build = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force $build | Out-Null
Set-Content -Encoding ascii (Join-Path $build "config.h") "#define TWITTERPS5_URL `"$Url`""

# Notificacao com o botao que abre o X no cartao do browser, ao lado do jogo.
$elf = Join-Path $build "TwitterPS5.elf"
$toast = Join-Path $PSScriptRoot "src\toast.c"
& $cc -Wall -Werror -O2 -I $build -o $elf (Join-Path $PSScriptRoot "src\main.c") $toast `
    -lSceNotification -lSceSystemService
if ($LASTEXITCODE -ne 0) { throw "a compilacao falhou" }

$hash = (Get-FileHash -Algorithm SHA256 $elf).Hash.ToLower()
Write-Host "OK: $elf ($((Get-Item $elf).Length) bytes) abre $Url"
Write-Host "SHA256 $hash"

# Diagnostico: mede a velocidade do cartao do browser (JS, layout, FPS, WebGL).
$diag = Join-Path $build "TwitterPS5-diag.elf"
& $cc -Wall -Werror -O2 -o $diag (Join-Path $PSScriptRoot "src\diag.c") $toast `
    -lSceNotification
if ($LASTEXITCODE -ne 0) { throw "a compilacao do diagnostico falhou" }
Write-Host "OK: $diag ($((Get-Item $diag).Length) bytes)"

# Diagnostico do ShadowMountPlus: pasta do titulo, montagem e o debug.log dele.
$smplog = Join-Path $build "TwitterPS5-smplog.elf"
& $cc -Wall -Werror -O2 -o $smplog (Join-Path $PSScriptRoot "src\smplog.c")
if ($LASTEXITCODE -ne 0) { throw "a compilacao do smplog falhou" }
Write-Host "OK: $smplog ($((Get-Item $smplog).Length) bytes)"

# Abre o titulo de teste por cima do jogo num so passo (suspende o jogo e
# lanca ou retoma o TWPS00001).
$open = Join-Path $build "TwitterPS5-open.elf"
& $cc -Wall -Werror -O2 -o $open (Join-Path $PSScriptRoot "src\open.c") `
    -lSceSystemService -lSceUserService
if ($LASTEXITCODE -ne 0) { throw "a compilacao do open falhou" }
Write-Host "OK: $open ($((Get-Item $open).Length) bytes)"

# Teste da timeline do X com a sessao do utilizador (cookies em
# /data/TwitterPS5/session.txt); mostra os posts em notificacoes. Leva os
# certificados de raiz de certs\, que os payloads nao recebem do sistema.
& python (Join-Path $PSScriptRoot "tools\embed.py") (Join-Path $build "certs.h") `
    "isrg_root_x1_pem=$(Join-Path $PSScriptRoot 'certs\isrg-root-x1.pem')" `
    "isrg_root_x2_pem=$(Join-Path $PSScriptRoot 'certs\isrg-root-x2.pem')" `
    "le_root_yr_pem=$(Join-Path $PSScriptRoot 'certs\le-root-yr-by-x1.pem')" `
    "le_yr1_pem=$(Join-Path $PSScriptRoot 'certs\le-yr1.pem')" `
    "le_yr2_pem=$(Join-Path $PSScriptRoot 'certs\le-yr2.pem')"
if ($LASTEXITCODE -ne 0) { throw "o embed dos certificados falhou" }
$xtest = Join-Path $build "TwitterPS5-xtest.elf"
& $cc -Wall -Werror -O2 -I $build -o $xtest (Join-Path $PSScriptRoot "src\xtest.c") `
    (Join-Path $PSScriptRoot "src\xapi.c") (Join-Path $PSScriptRoot "src\json.c") `
    (Join-Path $PSScriptRoot "src\tls.c") (Join-Path $PSScriptRoot "src\https.c") `
    (Join-Path $PSScriptRoot "src\play.c") `
    (Join-Path $PSScriptRoot "src\mem.c") -lSceNet -lSceSsl -lSceHttp
if ($LASTEXITCODE -ne 0) { throw "a compilacao do xtest falhou" }
Write-Host "OK: $xtest ($((Get-Item $xtest).Length) bytes)"

# Notificacoes do X por cima do jogo: fica a correr, le as notificacoes ao
# ritmo das Definicoes e mostra as novas como avisos da PS5; tambem le as
# capturas da PS5 para o titulo e serve a ligacao da conta pelo PC (o titulo
# nao pode escutar numa porta). Vai com o titulo, que o arranca, e enviado
# a mao outra vez, desliga-se.
& python (Join-Path $PSScriptRoot "tools\embed.py") (Join-Path $build "linkpage.h") `
    "link_page_html=$(Join-Path $PSScriptRoot 'src\linkpage\index.html')" `
    "link_page_js=$(Join-Path $PSScriptRoot 'src\linkpage\link.js')" `
    "link_page_css=$(Join-Path $PSScriptRoot 'src\linkpage\link.css')"
if ($LASTEXITCODE -ne 0) { throw "o embed da pagina de ligacao falhou" }
$notify = Join-Path $build "TwitterPS5-notify.elf"
& $cc -Wall -Werror -O2 -I $build -o $notify (Join-Path $PSScriptRoot "src\notify.c") `
    (Join-Path $PSScriptRoot "src\xnotify.c") (Join-Path $PSScriptRoot "src\xapi.c") `
    (Join-Path $PSScriptRoot "src\play.c") (Join-Path $PSScriptRoot "src\xsign.c") `
    (Join-Path $PSScriptRoot "src\xtid.c") `
    (Join-Path $PSScriptRoot "src\captures.c") (Join-Path $PSScriptRoot "src\settings.c") `
    (Join-Path $PSScriptRoot "src\game.c") (Join-Path $PSScriptRoot "src\trophy.c") `
    (Join-Path $PSScriptRoot "src\link.c") (Join-Path $PSScriptRoot "src\linkserve.c") `
    (Join-Path $PSScriptRoot "src\json.c") (Join-Path $PSScriptRoot "src\tls.c") `
    (Join-Path $PSScriptRoot "src\https.c") (Join-Path $PSScriptRoot "src\mem.c") `
    -lSceNet -lSceSsl -lSceHttp -lSceNotification -lSceSystemService -lSceUserService
if ($LASTEXITCODE -ne 0) { throw "a compilacao do notify falhou" }
Write-Host "OK: $notify ($((Get-Item $notify).Length) bytes)"

# Titulo TWPS00001 (categoria media) com a timeline do X, e o payload que o
# instala. O eboot e o libc.prx saem das ferramentas do PS5_Vulkan, no WSL
# (title\build-title.sh); sem -Wsl usa os que ja estao em build\title.
$title = Join-Path $build "title"
New-Item -ItemType Directory -Force $title | Out-Null
$ebootBin = Join-Path $title "eboot.bin"
$libcPrx = Join-Path $title "libc.prx"
# O icone e o fundo do menu da PS5 (title\art: 512x512, 1920x1080, e o fundo
# em BC7 3840x2160 que o menu mostra, feito por tools\bc7dds.c).
$iconPng = Join-Path $PSScriptRoot "title\art\icon0.png"
$picPng = Join-Path $PSScriptRoot "title\art\pic0.png"
$picDds = Join-Path $PSScriptRoot "title\art\pic0.dds"
if ($Wsl) {
    $toWsl = { param($p) "/mnt/" + $p.Substring(0, 1).ToLower() + ($p.Substring(2) -replace '\\', '/') }
    & wsl.exe -d $Wsl -- bash (& $toWsl (Join-Path $PSScriptRoot "title\build-title.sh")) (& $toWsl $title)
    if ($LASTEXITCODE -ne 0) { throw "a compilacao do titulo falhou" }
}
foreach ($f in @($ebootBin, $libcPrx, $iconPng, $picPng, $picDds)) {
    if (-not (Test-Path $f)) { throw "falta $f; corre com -Wsl Ubuntu-24.04" }
}

# Os ficheiros do titulo vao embebidos no payload como arrays C.
& python (Join-Path $PSScriptRoot "tools\embed.py") (Join-Path $build "title_files.h") `
    "eboot_bin=$ebootBin" "libc_prx=$libcPrx" "icon0_png=$iconPng" "pic0_png=$picPng" "pic_dds=$picDds" "notify_elf=$notify" `
    "param_json=$(Join-Path $PSScriptRoot 'title\param.json')"
if ($LASTEXITCODE -ne 0) { throw "o embed do titulo falhou" }

# O motor de login (tools/login-engine): com -Wsl, o pack-engine.sh poe-no em
# build\engine (xz, com a liblzma do SDK); o instalador leva-o e escreve-o em
# /data/TwitterPS5/engine quando muda. Sem ele, o instalador leva so o XPS5.
if ($Wsl) {
    & wsl.exe -d $Wsl -- bash (& $toWsl (Join-Path $PSScriptRoot "tools\login-engine\pack-engine.sh"))
    if ($LASTEXITCODE -ne 0) { throw "o pacote do motor de login falhou" }
}
$engine = Join-Path $build "engine"
$pack = Join-Path $engine "engine-files.tar.xz"
$engineArgs = @()
if (Test-Path $pack) {
    $sha = (Get-Content (Join-Path $engine "engine-files.sha256") -Raw).Trim()
    if ($sha -notmatch '^[0-9a-f]{64}$') { throw "engine-files.sha256 nao tem um SHA-256" }
    $packPath = $pack -replace '\\', '/'
    Set-Content -Encoding ascii (Join-Path $build "engine_pack.h") `
        "#define ENGINE_PACK_PATH `"$packPath`"`n#define ENGINE_PACK_SHA256 `"$sha`""
    $engineArgs = @("-DXPS5_ENGINE", "-I", (Join-Path $engine "include"),
                    (Join-Path $PSScriptRoot "src\tarout.c"), (Join-Path $engine "lib\liblzma.a"))
    Write-Host "o instalador leva o motor de login ($((Get-Item $pack).Length) bytes)"
} else {
    Write-Host "AVISO: sem $pack, o instalador nao leva o motor de login"
}

$install = Join-Path $build "XPS5.elf"
& $cc -Wall -Werror -O2 -I $build -o $install (Join-Path $PSScriptRoot "src\install.c") @engineArgs `
    -lSceSystemService
if ($LASTEXITCODE -ne 0) { throw "a compilacao do instalador falhou" }
Write-Host "OK: $install ($((Get-Item $install).Length) bytes) com o eboot de $((Get-Item $ebootBin).Length) bytes"

# O desinstalador: fecha o XPS5, para as notificacoes, tira-o do menu
# (pelo ShadowMountPlus, ou pelo sistema) e apaga os ficheiros dele.
$uninstall = Join-Path $build "XPS5-uninstall.elf"
& $cc -Wall -Werror -O2 -o $uninstall (Join-Path $PSScriptRoot "src\uninstall.c") `
    (Join-Path $PSScriptRoot "src\rmtree.c") -lSceSystemService -lSceAppInstUtil
if ($LASTEXITCODE -ne 0) { throw "a compilacao do desinstalador falhou" }
Write-Host "OK: $uninstall ($((Get-Item $uninstall).Length) bytes)"
