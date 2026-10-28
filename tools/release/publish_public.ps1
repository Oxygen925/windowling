# 공개 저장소 갱신 (릴리스마다). 개발은 비공개 저장소(origin)에서 하고, 공개 저장소(public)에는 릴리스마다 커밋 1개를 올린다.
# 단계를 나눠 하나씩 실행한다 (단계마다 결과를 보고 다음으로):
#   1) -Step Prepare : master 의 트리로 "Desktop Pet <버전>" 커밋을 public/master 위에 만든다 (로컬 브랜치 public-release)
#   2) -Step Push    : public-release 를 공개 저장소 master 로 push (fast-forward 만, 강제 push 안 함) → 공개 CI 시작
#   3) -Step Tag     : 공개 CI 가 통과했으면 v<버전> 태그 push → release.yml 이 릴리스 초안을 만든다
#   공개(초안 → 릴리스)는 사람이 확인한 뒤: gh release edit v<버전> --repo <공개 저장소> --draft=false --latest
# 버전은 CMakeLists.txt 의 project(VERSION) — 이미 공개한 버전이면 Tag 단계에서 멈춘다.
param(
    [Parameter(Mandatory)][ValidateSet("Prepare", "Push", "Tag")][string]$Step,
    [string]$Remote = "public",
    [string]$Origin = "origin",
    [string]$Branch = "public-release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

# git 이 stderr 에 쓰는 진행 메시지를 오류로 보지 않게 (Windows PowerShell 5.1), 종료 코드로만 판단
function Invoke-Git {
    $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $out = & git.exe @args 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE; $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "git $($args -join ' ') 실패: $out" }
    return $out
}
function RepoSlug($remote) {
    $url = Invoke-Git remote get-url $remote
    if ($url -notmatch 'github\.com[:/]([A-Za-z0-9-]+/[A-Za-z0-9._-]+?)(\.git)?$') { throw "GitHub 주소가 아님: $url" }
    return $Matches[1]
}

$cm = Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(\s*desktop_pet\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
if (-not $cm) { throw "CMakeLists.txt 에서 버전을 읽지 못함" }
$version = $cm.Matches[0].Groups[1].Value
$tag = "v$version"
$pub = RepoSlug $Remote

switch ($Step) {
    "Prepare" {
        # 작업 트리 깨끗, master, 비공개 원격과 같음
        if (Invoke-Git status --porcelain) { throw "커밋하지 않은 변경이 있음" }
        if ((Invoke-Git rev-parse --abbrev-ref HEAD) -ne "master") { throw "master 에서 실행" }
        Invoke-Git fetch -q $Origin | Out-Null
        if ((Invoke-Git rev-parse HEAD) -ne (Invoke-Git rev-parse "$Origin/master")) { throw "master 가 $Origin/master 와 다름 (push 먼저)" }
        # 작성자는 GitHub noreply 만 (공개 기록에 개인 이메일이 남지 않게)
        $name = Invoke-Git config user.name; $email = Invoke-Git config user.email
        if ($email -notlike "*@users.noreply.github.com") { throw "git user.email 이 noreply 가 아님: $email" }
        Invoke-Git fetch -q $Remote | Out-Null
        $base = "$Remote/master"
        $tree = Invoke-Git rev-parse "master^{tree}"
        if ((Invoke-Git rev-parse "$base^{tree}") -eq $tree) { Write-Host "공개 저장소와 내용이 같음 — 할 일 없음"; return }
        $env:GIT_AUTHOR_NAME = $name; $env:GIT_AUTHOR_EMAIL = $email
        $env:GIT_COMMITTER_NAME = $name; $env:GIT_COMMITTER_EMAIL = $email
        $commit = Invoke-Git commit-tree $tree -p (Invoke-Git rev-parse $base) -m "Desktop Pet $version"
        Invoke-Git branch -f $Branch $commit | Out-Null
        Write-Host "준비: $Branch = $($commit.Substring(0, 7)) (부모 $base, 버전 $version)"
        Invoke-Git diff --stat $base $Branch | Select-Object -Last 1 | Write-Host
        Write-Host "다음: -Step Push"
    }
    "Push" {
        Invoke-Git fetch -q $Remote | Out-Null
        $parent = Invoke-Git rev-parse "$Branch^"
        if ($parent -ne (Invoke-Git rev-parse "$Remote/master")) { throw "$Branch 가 $Remote/master 바로 위가 아님 — Prepare 를 다시" }
        if ((Invoke-Git rev-parse "$Branch^{tree}") -ne (Invoke-Git rev-parse "master^{tree}")) { throw "$Branch 트리가 master 와 다름 — Prepare 를 다시" }
        Invoke-Git push $Remote "${Branch}:master" | Write-Host
        Write-Host "push 완료. 공개 CI: gh run list --repo $pub --limit 3"
        Write-Host "CI 가 통과하면: -Step Tag"
    }
    "Tag" {
        Invoke-Git fetch -q --tags $Remote | Out-Null
        $sha = Invoke-Git rev-parse $Branch
        if ($sha -ne (Invoke-Git rev-parse "$Remote/master")) { throw "$Branch 가 공개 master 와 다름 — Push 먼저" }
        if (& git tag -l $tag) { throw "태그 $tag 가 이미 있음 — CMakeLists.txt 의 버전을 올릴 것" }
        $runs = gh run list --repo $pub --commit $sha --workflow ci.yml --json status,conclusion | ConvertFrom-Json
        if (-not $runs -or @($runs | Where-Object { $_.conclusion -eq "success" }).Count -eq 0) {
            throw "공개 CI 가 아직 통과하지 않음 ($($sha.Substring(0, 7))): $(($runs | ForEach-Object { "$($_.status)/$($_.conclusion)" }) -join ', ')"
        }
        Invoke-Git tag -a $tag -m "Desktop Pet $version" $sha | Out-Null
        Invoke-Git push $Remote $tag | Write-Host
        Write-Host "태그 $tag push — release.yml 이 초안을 만든다: gh run list --repo $pub --workflow release.yml --limit 1"
        Write-Host "초안 확인 뒤 공개: gh release edit $tag --repo $pub --draft=false --latest"
    }
}
