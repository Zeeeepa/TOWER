# Owl Browser Local Development Secrets

This file contains all credentials and secrets for local development environment.

> **WARNING**: These credentials are for LOCAL DEVELOPMENT ONLY. Never use these in production!

---

## License Server Credentials

### Admin Panel Access
- **URL**: http://localhost:3035
- **Username**: `admin`
- **Password**: `admin123`
- **Password Hash**: `scrypt:32768:8:1$4LK3l8Exc94Gzku8$a33a93d4d030af3110235e0e05c8b5d19db5af35abdae66eab6d1d77c254f4bd1d1858af9bd0b07643466a0890b050af591717c98e4e34fe8345b55f56a1216d`

### License API
- **URL**: http://localhost:3034
- **Health Check**: http://localhost:3034/api/v1/health

### Database (PostgreSQL)
- **Host**: localhost
- **Port**: 5432
- **Database**: `olib_licenses`
- **Username**: `olib`
- **Password**: `olib_dev_2024`

### Security Keys
- **Admin Secret Key**: `69c55c5d390d56c99bd4200473bcff99b0d163d8bda38fdd3cfa6f78ca9b5226`
- **HMAC Secret**: `39ba468791c375913c4e9bd3c9a6999d4e4b85c3cf87c2264bba73bcdfaebc37`

---

## Owl Browser Build Configuration

### Build Args (for Docker)
```bash
OWL_LICENSE_SERVER_URL=http://localhost:3034
OWL_NONCE_HMAC_SECRET=39ba468791c375913c4e9bd3c9a6999d4e4b85c3cf87c2264bba73bcdfaebc37
OWL_VM_PROFILE_DB_PASS=owl_vm_profiles_dev_2024
```

### HTTP Server Access
- **Panel URL**: http://localhost:8081
- **API URL**: http://localhost:8082
- **Panel Password**: `dev123`
- **API Token**: `test-token`

---

## RSA Keys Location

RSA keys for license signing are auto-generated on first license server start:
- **Private Key**: `packages/owl-browser/license-server/keys/private_key.pem`
- **Public Key**: `packages/owl-browser/license-server/keys/public_key.pem`

### Enable Full License Verification (Optional)

By default, the browser runs in LIMITED MODE because the embedded public key doesn't match the license server's key.

To enable full license verification:

```bash
# 1. Copy the public key from license server
cp packages/owl-browser/license-server/keys/public_key.pem ~/.owl_license/owl_license.pub

# 2. Update the embedded key in browser source
cd packages/owl-browser
npm run license:update-key

# 3. Rebuild the browser
npm run build
# OR rebuild Docker image
docker build -t owl-browser:latest -f docker/Dockerfile \
  --build-arg OWL_VM_PROFILE_DB_PASS=owl_vm_profiles_dev_2024 .
```

> **Note**: FULL MODE is now configured and working! The license server keys match the browser's embedded key.

---

## Docker Commands

### Start License Server
```bash
cd packages/owl-browser/license-server
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d
```

### Build Owl Browser
```bash
cd packages/owl-browser
docker build -t owl-browser:latest -f docker/Dockerfile \
  --build-arg OWL_VM_PROFILE_DB_PASS=owl_vm_profiles_dev_2024 \
  --build-arg OWL_NONCE_HMAC_SECRET=39ba468791c375913c4e9bd3c9a6999d4e4b85c3cf87c2264bba73bcdfaebc37 \
  --build-arg OWL_LICENSE_SERVER_URL=http://localhost:3034 .
```

### Run Owl Browser (LIMITED MODE)
```bash
docker run -d --name owl-browser \
  --add-host=host.docker.internal:host-gateway \
  -p 8081:80 -p 8082:8080 \
  -e OWL_PANEL_PASSWORD=dev123 \
  -e OWL_HTTP_TOKEN=test-token \
  owl-browser:latest
```

### Run Owl Browser (FULL MODE with License)
```bash
# First, generate and export license as base64
LICENSE_B64=$(base64 -w0 /path/to/your/license.olic)

# Run with license
docker run -d --name owl-browser \
  --add-host=host.docker.internal:host-gateway \
  -p 8081:80 -p 8082:8080 \
  -e OWL_PANEL_PASSWORD=dev123 \
  -e OWL_HTTP_TOKEN=test-token \
  -e OWL_LICENSE_CONTENT="$LICENSE_B64" \
  owl-browser:latest
```

**Current Dev License (base64):**
```
T1dMLUxJQ0VOU0V2MgAAAQAAAARuYW1lAAAIRGV2IFVzZXIAAAAAAQAAAAVlbWFpbAAADmRldkBsb2NhbC5kZXYAAAAAAQAAAA5vcmdhbml6YXRpb24AAAAAAAAAAQAAAA5oYXJkd2FyZV9ib3VuZAAAAQAAAAAAAQAAABNoYXJkd2FyZV9maW5nZXJwcmludAAAAAAAAAAAAQAAAA5mZWF0dXJlX2ZsYWdzAAAAAAAAAAAAAAEAAAAObGljZW5zZV9pZAAAJDU1MDZiZjA5LTljMTMtNDRjOC05NDM1LWE0OTE3ZjdmYzM2ZQAAAAABAAAADWxpY2Vuc2VfdHlwZQAAAAAABAAAAAABAAAACW1heF9zZWF0cwAAAAAAAAoAAAAAAQAAAAtpc3N1ZV9kYXRlAAAAAAAAZZ...
```

**License ID:** `5506bf09-9c13-44c8-9435-a4917f7fc36e`

---

## Web2API Backend

### Environment Variables
```bash
WEB2API_LLM_ENABLED=true
WEB2API_LLM_API_KEY=
WEB2API_LLM_BASE_URL=https://api.z.ai/api/openai/v1
WEB2API_LLM_MODEL=glm-4.6v
```

### Server
- **API URL**: http://localhost:8000
- **Health Check**: http://localhost:8000/health

---

## How to Generate New Secrets

### Generate Password Hash
```bash
python -c "from werkzeug.security import generate_password_hash; print(generate_password_hash('your_password'))"
```

### Generate Random Secret Key
```bash
python -c "import secrets; print(secrets.token_hex(32))"
```

### Generate RSA Key Pair
```bash
# In license-server directory
./build/license/license_generator keygen

# Or using openssl
openssl genpkey -algorithm RSA -out private_key.pem -pkeyopt rsa_keygen_bits:2048
openssl rsa -pubout -in private_key.pem -out public_key.pem
```

---

## Service URLs Summary

| Service | URL | Credentials |
|---------|-----|-------------|
| License Admin Panel | http://localhost:3035 | admin / admin123 |
| License API | http://localhost:3034 | (HMAC auth) |
| Owl Browser Panel | http://localhost:8081 | dev123 |
| Owl Browser API | http://localhost:8082 | Bearer test-token |
| Web2API Backend | http://localhost:8000 | - |

---

## Files Modified for Local Development

1. `packages/owl-browser/docker/Dockerfile` - Default URL changed to localhost
2. `packages/owl-browser/license-server/.env` - Local dev configuration
3. `packages/owl-browser/license-server/.env.example` - Template with localhost defaults
4. `packages/owl-browser/license-server/secrets.env` - Password hashes
5. `packages/owl-browser/.env` - Browser build configuration
