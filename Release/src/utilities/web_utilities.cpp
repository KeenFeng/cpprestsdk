/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved.
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* Credential and proxy utilities.
*
* For the latest on this and related APIs, please see http://casablanca.codeplex.com.
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/

#include "stdafx.h"

#if defined(_MS_WINDOWS) && !defined(__cplusplus_winrt)
#include <Wincrypt.h>
#endif

#if defined(__cplusplus_winrt)
#include <robuffer.h>
#endif

#include "cpprest\web_utilities.h"

namespace web
{
namespace details
{
#if defined(_MS_WINDOWS)
#if defined(__cplusplus_winrt)

// Helper function since SecureZeroMemory isn't available.
void winrt_secure_zero_memory(_Out_writes_(count) void *buffer, _In_ size_t count)
{
    auto vptr = reinterpret_cast<volatile char *>(buffer);
    while (count != 0)
    {
        *vptr = 0;
        ++vptr;
        --count;
    }
}
void winrt_secure_zero_buffer(Windows::Storage::Streams::IBuffer ^buffer)
{
    Microsoft::WRL::ComPtr<IInspectable> bufferInspectable(reinterpret_cast<IInspectable *>(buffer));
    Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
    bufferInspectable.As(&bufferByteAccess);
    byte * rawBytes;
    bufferByteAccess->Buffer(&rawBytes);
    winrt_secure_zero_memory(rawBytes, buffer->Length);
}

winrt_encryption::winrt_encryption(const std::wstring &data)
{
    auto provider = ref new Windows::Security::Cryptography::DataProtection::DataProtectionProvider(ref new Platform::String(L"Local=user"));

    // Create buffer containing plain text password.
    Platform::ArrayReference<unsigned char> arrayref(
        reinterpret_cast<unsigned char *>(const_cast<std::wstring::value_type *>(data.c_str())),
        data.size() * sizeof(std::wstring::value_type));
    Windows::Storage::Streams::IBuffer ^plaintext = Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(arrayref);
    m_buffer = pplx::create_task(provider->ProtectAsync(plaintext));
    m_buffer.then([plaintext](pplx::task<Windows::Storage::Streams::IBuffer ^>)
    {
        winrt_secure_zero_buffer(plaintext);
    });
}

plaintext_string winrt_encryption::decrypt() const
{
    // To fully guarantee asynchrony would require significant impact on existing code. This code path
    // is never run on a user's thread and is only done once when setting up a connection.
    auto encrypted = m_buffer.get();
    auto provider = ref new Windows::Security::Cryptography::DataProtection::DataProtectionProvider();
    auto plaintext = pplx::create_task(provider->UnprotectAsync(encrypted)).get();

    // Get access to raw bytes in plain text buffer.
    Microsoft::WRL::ComPtr<IInspectable> bufferInspectable(reinterpret_cast<IInspectable *>(plaintext));
    Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
    bufferInspectable.As(&bufferByteAccess);
    byte * rawPlaintext;
    bufferByteAccess->Buffer(&rawPlaintext);

    // Construct string and zero out memory from plain text buffer.
    auto data = plaintext_string(new std::wstring(
        reinterpret_cast<const std::wstring::value_type *>(rawPlaintext),
        plaintext->Length / 2));
    winrt_secure_zero_memory(rawPlaintext, plaintext->Length);
    return std::move(data);
}
#else
win32_encryption::win32_encryption(const std::wstring &data) :
    m_numCharacters(data.size())
{
    const auto dataNumBytes = data.size() * sizeof(std::wstring::value_type);
    m_buffer.resize(dataNumBytes);
    memcpy_s(m_buffer.data(), m_buffer.size(), data.c_str(), dataNumBytes);

    // Buffer must be a multiple of CRYPTPROTECTMEMORY_BLOCK_SIZE
    const auto mod = m_buffer.size() % CRYPTPROTECTMEMORY_BLOCK_SIZE;
    if (mod != 0)
    {
        m_buffer.resize(m_buffer.size() + CRYPTPROTECTMEMORY_BLOCK_SIZE - mod);
    }
    if (!CryptProtectMemory(m_buffer.data(), m_buffer.size(), CRYPTPROTECTMEMORY_SAME_PROCESS))
    {
        throw ::utility::details::create_system_error(GetLastError());
    }
}

win32_encryption::~win32_encryption()
{
    SecureZeroMemory(m_buffer.data(), m_buffer.size());
}

plaintext_string win32_encryption::decrypt() const
{
    // Copy the buffer and decrypt to avoid having to re-encrypt.
    auto data = plaintext_string(new std::wstring(reinterpret_cast<const std::wstring::value_type *>(m_buffer.data()), m_buffer.size() / 2));
    if (!CryptUnprotectMemory(
        const_cast<std::wstring::value_type *>(data->c_str()),
        m_buffer.size(),
        CRYPTPROTECTMEMORY_SAME_PROCESS))
    {
        throw ::utility::details::create_system_error(GetLastError());
    }
    data->resize(m_numCharacters);
    return std::move(data);
}
#endif

void zero_memory_deleter::operator()(::utility::string_t *data) const
{
#if defined(__cplusplus_winrt)
    winrt_secure_zero_memory(
#else
    SecureZeroMemory(
#endif
        const_cast<::utility::string_t::value_type *>(data->data()),
        data->size() * sizeof(::utility::string_t::value_type));
    delete data;
}

#endif
}

}