/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2019 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#include "protocols/driverControlClient.h"
#include "msgChannel.h"

#define DRIVERCONTROL_CLIENT_MIN_MAJOR_VERSION 1

namespace DevDriver
{
    namespace DriverControlProtocol
    {
        DriverControlClient::DriverControlClient(IMsgChannel* pMsgChannel)
            : BaseProtocolClient(pMsgChannel, Protocol::DriverControl, DRIVERCONTROL_CLIENT_MIN_MAJOR_VERSION, DRIVERCONTROL_PROTOCOL_MAJOR_VERSION)
        {
        }

        DriverControlClient::~DriverControlClient()
        {
        }

        Result DriverControlClient::PauseDriver()
        {
            Result result = Result::Error;

            if (IsConnected())
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<PauseDriverRequestPayload>();

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const PauseDriverResponsePayload& response =
                        container.GetPayload<PauseDriverResponsePayload>();
                    if (response.header.command == DriverControlMessage::PauseDriverResponse)
                    {
                        result = response.result;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::ResumeDriver()
        {
            Result result = Result::Error;

            if (IsConnected())
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<ResumeDriverRequestPayload>();

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const ResumeDriverResponsePayload& response =
                        container.GetPayload<ResumeDriverResponsePayload>();
                    if (response.header.command == DriverControlMessage::ResumeDriverResponse)
                    {
                        result = response.result;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::StepDriver(uint32 numSteps)
        {
            Result result = Result::Error;

            if (IsConnected() && numSteps > 0)
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<StepDriverRequestPayload>(numSteps);

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const StepDriverResponsePayload& response =
                        container.GetPayload<StepDriverResponsePayload>();
                    if (response.header.command == DriverControlMessage::StepDriverResponse)
                    {
                        result = response.result;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryNumGpus(uint32* pNumGpus)
        {
            Result result = Result::Error;

            if (IsConnected() && (pNumGpus != nullptr))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryNumGpusRequestPayload>();

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryNumGpusResponsePayload& response =
                        container.GetPayload<QueryNumGpusResponsePayload>();
                    if (response.header.command == DriverControlMessage::QueryNumGpusResponse)
                    {
                        result = response.result;
                        *pNumGpus = response.numGpus;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryDeviceClockMode(uint32 gpuIndex, DeviceClockMode* pClockMode)
        {
            Result result = Result::Error;

            if (IsConnected() && (pClockMode != nullptr))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryDeviceClockModeRequestPayload>(gpuIndex);

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryDeviceClockModeResponsePayload& response =
                        container.GetPayload<QueryDeviceClockModeResponsePayload>();
                    if (response.header.command == DriverControlMessage::QueryDeviceClockModeResponse)
                    {
                        result = response.result;
                        if (result == Result::Success)
                        {
                            *pClockMode = response.mode;
                        }
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryClientInfo(ClientInfoStruct* pClientInfo)
        {
            Result result = Result::Error;

            if (IsConnected() && (pClientInfo != nullptr) && (m_pSession->GetVersion() >= DRIVERCONTROL_QUERYCLIENTINFO_VERSION))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryClientInfoRequestPayload>();

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryClientInfoResponsePayload& response =
                        container.GetPayload<QueryClientInfoResponsePayload>();

                    if (response.header.command == DriverControlMessage::QueryClientInfoResponse)
                    {
                        *pClientInfo = response.clientInfo;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::SetDeviceClockMode(uint32 gpuIndex, DeviceClockMode clockMode)
        {
            Result result = Result::Error;

            if (IsConnected())
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<SetDeviceClockModeRequestPayload>(gpuIndex, clockMode);

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const SetDeviceClockModeResponsePayload& response =
                        container.GetPayload<SetDeviceClockModeResponsePayload>();

                    if (response.header.command == DriverControlMessage::SetDeviceClockModeResponse)
                    {
                        result = response.result;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryDeviceClock(uint32 gpuIndex, float* pGpuClock, float* pMemClock)
        {
            Result result = Result::Error;

            if (IsConnected() && (pGpuClock != nullptr) && (pMemClock != nullptr))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryDeviceClockRequestPayload>(gpuIndex);

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryDeviceClockResponsePayload& response =
                        container.GetPayload<QueryDeviceClockResponsePayload>();

                    if (response.header.command == DriverControlMessage::QueryDeviceClockResponse)
                    {
                        result = response.result;
                        if (result == Result::Success)
                        {
                            *pGpuClock = response.gpuClock;
                            *pMemClock = response.memClock;
                        }
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryMaxDeviceClock(uint32 gpuIndex, float* pMaxGpuClock, float* pMaxMemClock)
        {
            Result result = Result::Error;

            if (IsConnected() && (pMaxGpuClock != nullptr) && (pMaxMemClock != nullptr))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryMaxDeviceClockRequestPayload>(gpuIndex);

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryMaxDeviceClockResponsePayload& response =
                        container.GetPayload<QueryMaxDeviceClockResponsePayload>();

                    if (response.header.command == DriverControlMessage::QueryMaxDeviceClockResponse)
                    {
                        result = response.result;
                        if (result == Result::Success)
                        {
                            *pMaxGpuClock = response.maxGpuClock;
                            *pMaxMemClock = response.maxMemClock;
                        }
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::QueryDriverStatus(DriverStatus* pDriverStatus)
        {
            Result result = Result::Error;

            if (IsConnected() && (pDriverStatus != nullptr))
            {
                SizedPayloadContainer container = {};
                container.CreatePayload<QueryDriverStatusRequestPayload>();

                result = TransactDriverControlPayload(&container);
                if (result == Result::Success)
                {
                    const QueryDriverStatusResponsePayload& response =
                        container.GetPayload<QueryDriverStatusResponsePayload>();

                    if (response.header.command == DriverControlMessage::QueryDriverStatusResponse)
                    {
                        *pDriverStatus = response.status;
                    }
                    else
                    {
                        // Invalid response payload
                        result = Result::Error;
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::WaitForDriverInitialization(uint32 timeoutInMs)
        {
            Result result = Result::Error;
            if (IsConnected())
            {
                result = Result::VersionMismatch;
                if (GetSessionVersion() >= DRIVERCONTROL_INITIALIZATION_STATUS_VERSION)
                {
                    const uint64 startTime = Platform::GetCurrentTimeInMs();
                    const uint64 kQueryDelayInMs = 250;
                    uint64 nextQueryTime = startTime;
                    result = Result::Success;

                    while (result == Result::Success)
                    {
                        const uint64 currentTime = Platform::GetCurrentTimeInMs();
                        const uint64 totalElapsedTime = (currentTime - startTime);
                        if (totalElapsedTime >= timeoutInMs)
                        {
                            result = Result::NotReady;
                        }
                        else if (currentTime >= nextQueryTime)
                        {
                            nextQueryTime = currentTime + kQueryDelayInMs;

                            SizedPayloadContainer container = {};
                            container.CreatePayload<QueryDriverStatusRequestPayload>();

                            result = TransactDriverControlPayload(&container);
                            if (result == Result::Success)
                            {
                                const QueryDriverStatusResponsePayload& response =
                                    container.GetPayload<QueryDriverStatusResponsePayload>();

                                if (response.header.command == DriverControlMessage::QueryDriverStatusResponse)
                                {
                                    if ((response.status == DriverStatus::Running) ||
                                        (response.status == DriverStatus::Paused))
                                    {
                                        break;
                                    }
                                }
                                else
                                {
                                    // Invalid response payload
                                    result = Result::Error;
                                }
                            }
                        }
                    }
                }
            }

            return result;
        }

        Result DriverControlClient::SendDriverControlPayload(
            const SizedPayloadContainer& container,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            // Use the legacy size for the payload if we're connected to an older client, otherwise use the real size.
            const Version sessionVersion = (m_pSession.IsNull() == false) ? m_pSession->GetVersion() : 0;
            const uint32 payloadSize =
                (sessionVersion >= DRIVERCONTROL_QUERYCLIENTINFO_VERSION) ? container.payloadSize
                                                                          : kLegacyDriverControlPayloadSize;

            return SendSizedPayload(container.payload, payloadSize, timeoutInMs, retryInMs);
        }

        Result DriverControlClient::ReceiveDriverControlPayload(
            SizedPayloadContainer* pContainer,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            return ReceiveSizedPayload(pContainer->payload, sizeof(pContainer->payload), &pContainer->payloadSize, timeoutInMs, retryInMs);
        }

        Result DriverControlClient::TransactDriverControlPayload(
            SizedPayloadContainer* pContainer,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            Result result = SendDriverControlPayload(*pContainer, timeoutInMs, retryInMs);
            if (result == Result::Success)
            {
                result = ReceiveDriverControlPayload(pContainer, timeoutInMs, retryInMs);
            }

            return result;
        }
    }

} // DevDriver
