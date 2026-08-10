import os
import subprocess
from pathlib import Path

Import("env")

# Get the project directory
project_dir = Path(env.subst("$PROJECT_DIR"))

# Check if we're in a git repository
git_dir = project_dir / ".git"
if not git_dir.exists():
    print("Not a git repository, skipping submodule update")
    exit(0)

try:
    # Update submodules
    print("Updating git submodules...")
    result = subprocess.run(
        ["git", "submodule", "update", "--init", "--recursive"],
        cwd=str(project_dir),
        capture_output=True,
        text=True
    )
    
    if result.returncode == 0:
        print("✓ Submodules updated successfully")
        if result.stdout:
            print(result.stdout)
    else:
        print("✗ Warning: Failed to update submodules")
        if result.stderr:
            print(result.stderr)
        # Don't fail the build, just warn
        
except Exception as e:
    print(f"✗ Warning: Error updating submodules: {e}")
    # Don't fail the build, just warn
