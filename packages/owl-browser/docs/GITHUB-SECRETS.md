# GitHub Secrets Configuration

This document lists all the secrets required to run the CI/CD workflows for Owl Browser.

## Quick Setup

Go to **Repository Settings → Secrets and variables → Actions → New repository secret** and add the following:

| Secret Name | Required | Description |
|-------------|----------|-------------|
| `OWL_NONCE_HMAC_SECRET` | Yes | HMAC secret for license validation |
| `OWL_VM_PROFILE_DB_PASS` | Yes | VM profiles database encryption password |
| `NPM_TOKEN` | Yes | npm token for SDK publishing |
| `PYPI_API_TOKEN` | Yes | PyPI token for Python SDK publishing |
| `TEST_PYPI_API_TOKEN` | No | Test PyPI token (optional) |

---

## Secret Details

### `OWL_NONCE_HMAC_SECRET`

**Required:** Yes
**Used in:** CI builds, Docker builds, Releases

HMAC secret used for license nonce validation during the C++ build process.

**Generate:**
```bash
openssl rand -hex 32
```

---

### `OWL_VM_PROFILE_DB_PASS`

**Required:** Yes
**Used in:** Docker builds, Releases

Password for encrypting the VM profiles SQLite database (SQLCipher).

**Generate:**
```bash
openssl rand -base64 32
```

---

### `NPM_TOKEN`

**Required:** Yes (for releases)
**Used in:** Release workflow, SDK manual publish

npm access token for publishing the `@olib-ai/owl-browser-sdk` package.

**How to get:**
1. Go to https://www.npmjs.com/settings/~/tokens
2. Click **Generate New Token** → **Classic Token**
3. Select **Automation** type
4. Copy the token

---

### `PYPI_API_TOKEN`

**Required:** Yes (for releases)
**Used in:** Release workflow, Python SDK manual publish

PyPI API token for publishing the `owl-browser` Python package.

**How to get:**
1. Go to https://pypi.org/manage/account/token/
2. Click **Add API token**
3. Token name: `owl-browser-ci`
4. Scope: `Entire account` or project-specific
5. Copy the token (starts with `pypi-`)

---

### `TEST_PYPI_API_TOKEN`

**Required:** No (optional)
**Used in:** Python SDK manual publish (test mode)

Test PyPI API token for testing Python package publishing before production release.

**How to get:**
1. Go to https://test.pypi.org/manage/account/token/
2. Click **Add API token**
3. Copy the token

---

## Workflow Usage Matrix

| Workflow | `OWL_NONCE_HMAC_SECRET` | `OWL_VM_PROFILE_DB_PASS` | `NPM_TOKEN` | `PYPI_API_TOKEN` |
|----------|-------------------------|--------------------------|-------------|------------------|
| `ci.yml` | ✓ | | | |
| `docker.yml` | ✓ | ✓ | | |
| `release.yml` | ✓ | ✓ | ✓ | ✓ |
| `sdk-publish.yml` | | | ✓ | |
| `python-sdk-publish.yml` | | | | ✓ |

---

## Auto-Provided Secrets

The following secret is automatically provided by GitHub Actions and does **not** need to be configured:

- `GITHUB_TOKEN` - Used for GitHub API access, container registry, and releases

---

## Verification

After adding all secrets, you can verify them by:

1. Go to **Settings → Secrets and variables → Actions**
2. Confirm all required secrets are listed under "Repository secrets"
3. Trigger a CI run on a branch to test the build

---

## Troubleshooting

### Build fails with "OWL_NONCE_HMAC_SECRET not set"
- Ensure the secret is added with the exact name (case-sensitive)
- Check the secret has a value (not empty)

### Docker build fails with "OWL_VM_PROFILE_DB_PASS required"
- This secret is required for Docker builds
- Generate a secure password and add it

### npm publish fails with "401 Unauthorized"
- Regenerate your npm token
- Ensure the token has "Automation" permissions
- Check the token hasn't expired

### PyPI publish fails with "403 Forbidden"
- Ensure the token scope includes the `owl-browser` package
- Try creating a new token with "Entire account" scope
