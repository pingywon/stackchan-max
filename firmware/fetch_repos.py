import os
import subprocess
import json


def clone_or_update_repo(
    repo_url, path, ref=None, with_submodules=False, patch_path=None, extra_patches=None
):
    import os

    if not os.path.exists(path):
        subprocess.run(["git", "clone", repo_url, path], check=True)
    else:
        subprocess.run(["git", "-C", path, "fetch"], check=True)

    if ref:
        subprocess.run(["git", "-C", path, "checkout", ref], check=True)

    if with_submodules:
        subprocess.run(
            ["git", "-C", path, "submodule", "update", "--init", "--recursive"],
            check=True,
        )

    # 应用 patch
    if patch_path:
        patch_full_path = (
            patch_path
            if os.path.isabs(patch_path)
            else os.path.join(os.getcwd(), patch_path)
        )
        # 使用 git apply --check 先检测补丁是否能应用，避免报错
        check_result = subprocess.run(
            ["git", "-C", path, "apply", "--check", patch_full_path]
        )
        if check_result.returncode == 0:
            subprocess.run(["git", "-C", path, "apply", patch_full_path], check=True)
            print(f"Applied patch {patch_path} to {path}")
        else:
            print(f"Patch {patch_path} cannot be applied cleanly to {path}, skipped.")

    # Fork-local patches, applied after the upstream one. Kept separate from
    # M5Stack's patch so their file stays pristine and upstream merges stay easy.
    for extra in extra_patches or []:
        extra_full = extra if os.path.isabs(extra) else os.path.join(os.getcwd(), extra)
        check = subprocess.run(["git", "-C", path, "apply", "--check", extra_full])
        if check.returncode == 0:
            subprocess.run(["git", "-C", path, "apply", extra_full], check=True)
            print(f"Applied patch {extra} to {path}")
        else:
            print(f"Patch {extra} cannot be applied cleanly to {path}, skipped.")


def fetch_dependencies():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "repos.json")

    with open(config_path) as f:
        repos = json.load(f)

    for repo in repos:
        repo_path = os.path.join(script_dir, repo["path"])
        branch = repo.get("branch")
        with_submodules = repo.get("with_submodules", False)
        patch = repo.get("patch")
        if patch and not os.path.isabs(patch):
            patch = os.path.join(script_dir, patch)
        extra_patches = repo.get("extra_patches") or []
        extra_patches = [
            p if os.path.isabs(p) else os.path.join(script_dir, p) for p in extra_patches
        ]
        clone_or_update_repo(
            repo["url"], repo_path, branch, with_submodules, patch, extra_patches
        )


if __name__ == "__main__":
    fetch_dependencies()