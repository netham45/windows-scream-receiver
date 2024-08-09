#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define REFTIMES_PER_SEC  10000000
#include <iostream>
#include <winsock2.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <string>
#include <ws2ipdef.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <atlbase.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

#define BUFFER_SIZE 1152
#define HEADER_SIZE 5

void Log(const std::string& message) {
    printf("%s\n", message.c_str());
}

void LogError(const std::string& message, HRESULT hr) {
    printf("%i : %s\n", hr, message.c_str());
}

HRESULT PlayAudio(SOCKET sock) {
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioClient* pAudioClient = NULL;
    IAudioRenderClient* pRenderClient = NULL;
    WAVEFORMATEXTENSIBLE* pwfex = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
    ZeroMemory(pwfex, sizeof(WAVEFORMATEXTENSIBLE));
    UINT32 bufferFrameCount;
    UINT32 numFramesPadding;
    UINT32 numFramesAvailable;
    BYTE* pData;
    char buffer[BUFFER_SIZE + HEADER_SIZE] = { 0 };
    while (true) {
        int recvResult = recv(sock, buffer, BUFFER_SIZE + HEADER_SIZE, 0);
        if (recvResult == SOCKET_ERROR) {
            Log("Failed to receive data over UDP");
            break;
        }

        int sample_mask = buffer[0];
        bool is_44100 = (sample_mask >> 7) & 1;
        int sample_rate = (sample_mask & 0x7F) * (is_44100 ? 44100 : 48000);
        if (pwfex->Format.nChannels != buffer[2] || pwfex->Format.nSamplesPerSec != sample_rate || pwfex->Format.wBitsPerSample != buffer[1]) {
            Log("Audio format changed. Reinitializing audio client.");
            Log("Old Sample Rate: " + std::to_string(pwfex->Format.nSamplesPerSec) + " New Sample Rate: " + std::to_string(sample_rate));
            Log("Old Bit Depth: " + std::to_string(pwfex->Format.wBitsPerSample) + " New Bit Depth: " + std::to_string(buffer[1]));
            Log("Old Channels: " + std::to_string(pwfex->Format.nChannels) + " New Channels: " + std::to_string(buffer[2]));
            if (pAudioClient)pAudioClient->Stop();
            CoTaskMemFree(pwfex);
            if (pRenderClient)pRenderClient->Release();
            if (pAudioClient)pAudioClient->Release();
            if (pDevice)pDevice->Release();
            if (pEnumerator)pEnumerator->Release();
            pwfex = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
            HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

            if (FAILED(hr)) {
                LogError("Failed to create device enumerator", hr);
                return hr;
            }

            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
            if (FAILED(hr)) {
                LogError("Failed to get default audio endpoint", hr);
                pEnumerator->Release();
                return hr;
            }

            hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
            if (FAILED(hr)) {
                LogError("Failed to activate audio client", hr);
                pDevice->Release();
                pEnumerator->Release();
                return hr;
            }

            hr = pAudioClient->GetMixFormat((WAVEFORMATEX**)&pwfex);
            if (FAILED(hr)) {
                LogError("Failed to get mix format", hr);
                pAudioClient->Release();
                pDevice->Release();
                pEnumerator->Release();
                return hr;
            }

            pwfex->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            pwfex->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            pwfex->Format.nChannels = buffer[2];
            pwfex->Format.nSamplesPerSec = sample_rate;
            pwfex->Format.wBitsPerSample = buffer[1];
            pwfex->Format.nBlockAlign = pwfex->Format.nChannels * pwfex->Format.wBitsPerSample / 8;
            pwfex->Format.nAvgBytesPerSec = pwfex->Format.nSamplesPerSec * pwfex->Format.nBlockAlign;
            pwfex->dwChannelMask = (static_cast<DWORD>(buffer[3]) << 8) | static_cast<DWORD>(buffer[4]);
            pwfex->Samples.wValidBitsPerSample = pwfex->Format.wBitsPerSample;
            pwfex->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

            hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, REFTIMES_PER_SEC, 0, (WAVEFORMATEX*)pwfex, NULL);
            if (FAILED(hr)) {
                LogError("Failed to initialize audio client", hr);
                CoTaskMemFree(pwfex);
                pAudioClient->Release();
                pDevice->Release();
                pEnumerator->Release();
                return hr;
            }

            hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
            if (FAILED(hr)) {
                LogError("Failed to get audio render client", hr);
                CoTaskMemFree(pwfex);
                pAudioClient->Release();
                pDevice->Release();
                pEnumerator->Release();
                return hr;
            }

            hr = pAudioClient->Start();
            if (FAILED(hr)) {
                LogError("Failed to start audio client", hr);
                pRenderClient->Release();
                CoTaskMemFree(pwfex);
                pAudioClient->Release();
                pDevice->Release();
                pEnumerator->Release();
                return hr;
            }
        }

        UINT32 bytesToWrite = recvResult - HEADER_SIZE;
        BYTE* pAudioData = reinterpret_cast<BYTE*>(buffer + HEADER_SIZE);

        while (bytesToWrite > 0 && pAudioClient) {
            HRESULT hr = pAudioClient->GetBufferSize(&bufferFrameCount);
            if (FAILED(hr)) {
                LogError("Failed to get buffer size", hr);
                return hr;
            }

            hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
            if (FAILED(hr)) {
                LogError("Failed to get padding", hr);
                return hr;
            }

            numFramesAvailable = bufferFrameCount - numFramesPadding;

            hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
            if (FAILED(hr)) {
                LogError("Failed to get buffer", hr);
                return hr;
            }

            UINT32 framesToWrite = min(numFramesAvailable, bytesToWrite / pwfex->Format.nBlockAlign);
            UINT32 bytesWritten = framesToWrite * pwfex->Format.nBlockAlign;

            memcpy(pData, pAudioData, bytesWritten);
            pAudioData += bytesWritten;
            bytesToWrite -= bytesWritten;

            hr = pRenderClient->ReleaseBuffer(framesToWrite, 0);
            if (FAILED(hr)) {
                LogError("Failed to release buffer", hr);
                return hr;
            }
        }
    }
    return S_OK;
}

int main(int argc, char* argv[]) {
    CoInitialize(NULL);
    std::string MULTICAST_GROUP("239.255.77.77");
    int LOCAL_PORT = 4010;

    if (argc > 1)
        LOCAL_PORT = atoi(argv[1]);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Log("WSAStartup failed");
        CoUninitialize();
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        Log("Socket creation failed");
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(LOCAL_PORT);
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        Log("Bind failed");
        closesocket(sock);
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    // Set up multicast
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        Log("setsockopt for IP_ADD_MEMBERSHIP failed");
        closesocket(sock);
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    Log("Listening on multicast group " + MULTICAST_GROUP + " port " + std::to_string(LOCAL_PORT));

    while (true)
        PlayAudio(sock);

    closesocket(sock);
    WSACleanup();
    CoUninitialize();

    Log("Application ended");

    return 0;
}
