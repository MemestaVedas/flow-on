# GitHub Setup Script for FLOW-ON!
# This script helps you push your project to GitHub for the first time

param(
    [Parameter(Mandatory=$false)]
    [string]$RepoUrl,
    
    [Parameter(Mandatory=$false)]
    [switch]$CreateRepo,
    
    [Parameter(Mandatory=$false)]
    [string]$RepoName = "flow-on",
    
    [Parameter(Mandatory=$false)]
    [switch]$Help
)

function Show-Help {
    Write-Host @"
GitHub Setup Script for FLOW-ON!

Usage:
  .\setup-github.ps1 -RepoUrl <url>        # Push to existing repository
  .\setup-github.ps1 -CreateRepo           # Create new repository (requires gh CLI)
  .\setup-github.ps1 -Help                 # Show this help

Examples:
  # Push to existing GitHub repository
  .\setup-github.ps1 -RepoUrl "https://github.com/username/flow-on.git"
  
  # Create new repository and push (requires GitHub CLI)
  .\setup-github.ps1 -CreateRepo -RepoName "flow-on"
  
  # Manual setup (no script)
  git remote add origin https://github.com/username/flow-on.git
  git branch -M main
  git push -u origin main

Prerequisites:
  - Git configured with user.name and user.email
  - GitHub account
  - (Optional) GitHub CLI (gh) for automated repo creation

"@
    exit 0
}

if ($Help) {
    Show-Help
}

Write-Host "=== FLOW-ON! GitHub Setup ===" -ForegroundColor Cyan
Write-Host ""

# Check git status
$status = git status --porcelain
if ($status) {
    Write-Host "Warning: You have uncommitted changes:" -ForegroundColor Yellow
    git status --short
    Write-Host ""
    $continue = Read-Host "Continue anyway? (y/N)"
    if ($continue -ne "y" -and $continue -ne "Y") {
        Write-Host "Aborted." -ForegroundColor Red
        exit 1
    }
}

# Check if remote already exists
$existingRemote = git remote get-url origin 2>$null
if ($existingRemote) {
    Write-Host "Remote 'origin' already configured:" -ForegroundColor Green
    Write-Host "  $existingRemote"
    Write-Host ""
    $overwrite = Read-Host "Overwrite existing remote? (y/N)"
    if ($overwrite -eq "y" -or $overwrite -eq "Y") {
        git remote remove origin
        Write-Host "Removed existing remote." -ForegroundColor Yellow
    } else {
        Write-Host "Keeping existing remote. You can push with:" -ForegroundColor Cyan
        Write-Host "  git push -u origin main"
        exit 0
    }
}

# Option 1: Create new repository with GitHub CLI
if ($CreateRepo) {
    Write-Host "[1/3] Creating GitHub repository..." -ForegroundColor Cyan
    
    # Check if gh CLI is installed
    $ghInstalled = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $ghInstalled) {
        Write-Host "Error: GitHub CLI (gh) not found." -ForegroundColor Red
        Write-Host "Install: scoop install gh  OR  choco install gh" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Or create repository manually at https://github.com/new" -ForegroundColor Yellow
        exit 1
    }
    
    # Check gh auth status
    $authStatus = gh auth status 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Not authenticated with GitHub CLI." -ForegroundColor Red
        Write-Host "Run: gh auth login" -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host "Creating repository '$RepoName'..."
    $visibility = Read-Host "Repository visibility (public/private) [public]"
    if (-not $visibility) { $visibility = "public" }
    
    $description = "Professional Windows voice-to-text tool. Local-first, zero-cloud, zero-telemetry."
    
    gh repo create $RepoName --$visibility --description $description --source . --remote origin
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to create repository." -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Repository created successfully!" -ForegroundColor Green
    $RepoUrl = (git remote get-url origin)
}

# Option 2: Use provided repository URL
if ($RepoUrl) {
    Write-Host "[1/3] Adding remote..." -ForegroundColor Cyan
    Write-Host "  Repository: $RepoUrl"
    
    git remote add origin $RepoUrl
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to add remote." -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Remote added successfully!" -ForegroundColor Green
}

# If no options provided, prompt user
if (-not $CreateRepo -and -not $RepoUrl) {
    Write-Host "No repository specified. Choose an option:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "1. I already created a repository on GitHub (enter URL)"
    Write-Host "2. Create a new repository with GitHub CLI (requires 'gh')"
    Write-Host "3. Exit and create repository manually"
    Write-Host ""
    
    $choice = Read-Host "Enter choice (1-3)"
    
    switch ($choice) {
        "1" {
            $RepoUrl = Read-Host "Enter repository URL (e.g., https://github.com/username/flow-on.git)"
            if ($RepoUrl) {
                git remote add origin $RepoUrl
                Write-Host "Remote added!" -ForegroundColor Green
            } else {
                Write-Host "Error: Invalid URL." -ForegroundColor Red
                exit 1
            }
        }
        "2" {
            Write-Host "Creating repository with GitHub CLI..."
            & $PSCommandPath -CreateRepo -RepoName $RepoName
            exit $LASTEXITCODE
        }
        "3" {
            Write-Host ""
            Write-Host "Create your repository at: https://github.com/new" -ForegroundColor Cyan
            Write-Host "Then run:" -ForegroundColor Cyan
            Write-Host "  git remote add origin <your-repo-url>"
            Write-Host "  git branch -M main"
            Write-Host "  git push -u origin main"
            exit 0
        }
        default {
            Write-Host "Invalid choice." -ForegroundColor Red
            exit 1
        }
    }
}

# Rename branch to main (GitHub standard)
Write-Host ""
Write-Host "[2/3] Renaming branch to 'main'..." -ForegroundColor Cyan
$currentBranch = git branch --show-current
if ($currentBranch -ne "main") {
    git branch -M main
    Write-Host "Branch renamed: $currentBranch -> main" -ForegroundColor Green
} else {
    Write-Host "Already on 'main' branch" -ForegroundColor Green
}

# Push to GitHub
Write-Host ""
Write-Host "[3/3] Pushing to GitHub..." -ForegroundColor Cyan
Write-Host "  This will upload all commits and files to GitHub"
Write-Host ""

$confirm = Read-Host "Push now? (Y/n)"
if ($confirm -eq "n" -or $confirm -eq "N") {
    Write-Host ""
    Write-Host "Setup complete! When ready, push with:" -ForegroundColor Cyan
    Write-Host "  git push -u origin main" -ForegroundColor White
    exit 0
}

git push -u origin main

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  Successfully pushed to GitHub!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    $repoUrl = git remote get-url origin
    $webUrl = $repoUrl -replace '\.git$', '' -replace 'git@github\.com:', 'https://github.com/'
    
    Write-Host "Your repository:" -ForegroundColor Cyan
    Write-Host "  $webUrl" -ForegroundColor White
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Visit your repository to see the code"
    Write-Host "  2. GitHub Actions will automatically build on every push"
    Write-Host "  3. Check Actions tab for build status"
    Write-Host "  4. Add topics: voice-to-text, whisper-cpp, windows, cpp"
    Write-Host ""
    
    # Open browser (optional)
    $openBrowser = Read-Host "Open repository in browser? (Y/n)"
    if ($openBrowser -ne "n" -and $openBrowser -ne "N") {
        Start-Process $webUrl
    }
} else {
    Write-Host ""
    Write-Host "Error: Push failed." -ForegroundColor Red
    Write-Host "Common issues:" -ForegroundColor Yellow
    Write-Host "  - Authentication required (configure Git credentials or SSH key)"
    Write-Host "  - Repository doesn't exist (create it on GitHub first)"
    Write-Host "  - Branch protection rules (check repository settings)"
    Write-Host ""
    Write-Host "Retry with:" -ForegroundColor Cyan
    Write-Host "  git push -u origin main"
    exit 1
}
