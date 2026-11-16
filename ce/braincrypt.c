#include <windows.h>

typedef ULONG crypt_handle;

BOOL WINAPI CeGenRandom(DWORD length, BYTE *buffer);

static BOOL crypto_unsupported(void)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

BOOL WINAPI CryptAcquireContextW(crypt_handle *provider, LPCWSTR container, LPCWSTR provider_name, DWORD provider_type, DWORD flags)
{
    if (provider)
        *provider = 0;
    return crypto_unsupported();
}

BOOL WINAPI CryptReleaseContext(crypt_handle provider, DWORD flags)
{
    return TRUE;
}

BOOL WINAPI CryptDeriveKey(crypt_handle provider, DWORD algorithm, crypt_handle hash, DWORD flags, crypt_handle *key)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptDestroyKey(crypt_handle key)
{
    return TRUE;
}

BOOL WINAPI CryptGetKeyParam(crypt_handle key, DWORD parameter, BYTE *data, DWORD *length, DWORD flags)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptImportKey(crypt_handle provider, const BYTE *data, DWORD length, crypt_handle public_key, DWORD flags, crypt_handle *key)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptCreateHash(crypt_handle provider, DWORD algorithm, crypt_handle key, DWORD flags, crypt_handle *hash)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptHashData(crypt_handle hash, const BYTE *data, DWORD length, DWORD flags)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptDestroyHash(crypt_handle hash)
{
    return TRUE;
}

BOOL WINAPI CryptGetUserKey(crypt_handle provider, DWORD key_specification, crypt_handle *key)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptGetProvParam(crypt_handle provider, DWORD parameter, BYTE *data, DWORD *length, DWORD flags)
{
    return crypto_unsupported();
}

BOOL WINAPI CryptEnumProvidersW(DWORD index, DWORD *reserved, DWORD flags, DWORD *provider_type, LPWSTR name, DWORD *name_length)
{
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

BOOL WINAPI CryptGenRandom(crypt_handle provider, DWORD length, BYTE *buffer)
{
    return CeGenRandom(length, buffer);
}
