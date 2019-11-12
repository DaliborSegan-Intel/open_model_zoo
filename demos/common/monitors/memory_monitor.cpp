// Copyright (C) 2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "memory_monitor.h"

#ifdef _WIN32
#include <algorithm>

#define PSAPI_VERSION 1 // for psapi
#include <windows.h>
#include <psapi.h>

namespace {
double getMemTotalOnly() {
    PERFORMANCE_INFORMATION performanceInformation;
    if (!GetPerformanceInfo(&performanceInformation, sizeof(performanceInformation))) {
        throw std::runtime_error("GetPerformanceInfo() failed");
    }
    return static_cast<double>(performanceInformation.PhysicalTotal * performanceInformation.PageSize)
        / (1024 * 1024 * 1024);
}

double getSwapTotalOnly() {
    PERFORMANCE_INFORMATION performanceInformation;
    if (!GetPerformanceInfo(&performanceInformation, sizeof(performanceInformation))) {
        throw std::runtime_error("GetPerformanceInfo() failed");
    }
    return static_cast<double>(performanceInformation.CommitLimit * performanceInformation.PageSize)
        / (1024 * 1024 * 1024);
}
}

MemoryMonitor::MemoryMonitor() :
    enabled{false},
    samplesNumber{0},
    meanMem{0},
    meanSwap{0},
    maxMem{0},
    maxSwap{0},
    memTotal{getMemTotalOnly()},
    swapTotal{getSwapTotalOnly()},
    maxMemTotal{memTotal},
    maxSwapTotal{swapTotal} {}

bool MemoryMonitor::isEnabled() const {
    return enabled;
}

void MemoryMonitor::enable(std::size_t historySize) {
    this->historySize = historySize;
    enabled = true;
}

void MemoryMonitor::disable() {
    enabled = false;
}

void MemoryMonitor::collectData() {
    PERFORMANCE_INFORMATION performanceInformation;
    if (!GetPerformanceInfo(&performanceInformation, sizeof(performanceInformation))) {
        throw std::runtime_error("GetPerformanceInfo() failed");
    }
    double usedMem = static_cast<double>(
        (performanceInformation.PhysicalTotal - performanceInformation.PhysicalAvailable)
        * performanceInformation.PageSize) / (1024 * 1024 * 1024);
    double usedSwap = static_cast<double>(performanceInformation.CommitTotal * performanceInformation.PageSize)
        / (1024 * 1024 * 1024);
    memTotal = static_cast<double>(performanceInformation.PhysicalTotal * performanceInformation.PageSize)
            / (1024 * 1024 * 1024),
    swapTotal = static_cast<double>(performanceInformation.CommitLimit * performanceInformation.PageSize)
            / (1024 * 1024 * 1024);
    maxMemTotal = std::max(maxMemTotal, memTotal);
    maxSwapTotal = std::max(maxSwapTotal, swapTotal);

    if (0 == samplesNumber) {
        maxMem = meanMem = usedMem;
        maxSwap = meanSwap = usedSwap;
        samplesNumber = 1;
    } else {
        meanMem = (meanMem * samplesNumber + usedMem) / (samplesNumber + 1);
        meanSwap = (meanSwap * samplesNumber + usedSwap) / (samplesNumber + 1);
        ++samplesNumber;
        maxMem = std::max(maxMem, usedMem);
        maxSwap = std::max(maxSwap, usedSwap);
    }
    memSwapUsageHistory.emplace_back(usedMem, usedSwap);
    if (memSwapUsageHistory.size() > historySize) {
        memSwapUsageHistory.pop_front();
    }
}

std::deque<std::pair<double, double>> MemoryMonitor::getLastHistory() const {
    return memSwapUsageHistory;
}

double MemoryMonitor::getMeanMem() const {
    return meanMem;
}

double MemoryMonitor::getMeanSwap() const {
    return meanSwap;
}

double MemoryMonitor::getMaxMem() const {
    return maxMem;
}

double MemoryMonitor::getMaxSwap() const {
    return maxSwap;
}

double MemoryMonitor::getMemTotal() const {
    return memTotal;
}

double MemoryMonitor::getSwapTotal() const {
    return swapTotal;
}

double MemoryMonitor::getMaxMemTotal() const {
    return maxMemTotal;
}

double MemoryMonitor::getMaxSwapTotal() const {
    return maxSwapTotal;
}
#else
#include <fstream>
#include <utility>
#include <vector>
#include <regex>

namespace {
double getMemTotalOnly() {
    double memTotal = 0;
    std::regex memRegex("^(.+):\\s+(\\d+) kB$");
    std::string line;
    std::smatch match;
    std::ifstream meminfo("/proc/meminfo");
    std::getline(meminfo, line);
    while(meminfo.good())
    {
        if (std::regex_match(line, match, memRegex))
        {
            if ("MemTotal" == match[1]) {
                memTotal = stod(match[2]) / (1024 * 1024);
            }
        }
        std::getline(meminfo, line);
    }
    if (0 == memTotal) {
        throw std::runtime_error("Can't get total memory");
    } else {
        return memTotal;
    }
}

double getSwapTotalOnly() {
    double swapTotal = 0;
    std::regex memRegex("^(.+):\\s+(\\d+) kB$");
    std::string line;
    std::smatch match;
    std::ifstream meminfo("/proc/meminfo");
    std::getline(meminfo, line);
    while(meminfo.good())
    {
        if (std::regex_match(line, match, memRegex))
        {
            if ("SwapTotal" == match[1]) {
                swapTotal = stod(match[2]) / (1024 * 1024);
            }
        }
        std::getline(meminfo, line);
    }
    if (0 == swapTotal) {
        throw std::runtime_error("Can't get total memory");
    } else {
        return swapTotal;
    }
}
}

std::pair<std::pair<double, double>, std::pair<double, double>> getAvailableMemSwapTotalMemSwap() {
    double availableMem = 0, availableSwap = 0, memTotal = 0, swapTotal = 0;
    unsigned long memfree = 0, buffers = 0, cached = 0, sReclaimable = 0, shmem = 0;
    std::regex memRegex("^(.+):\\s+(\\d+) kB$");
    std::string line;
    std::smatch match;
    std::ifstream meminfo("/proc/meminfo");
    std::getline(meminfo, line);
    while(meminfo.good())
    {
        if (std::regex_match(line, match, memRegex))
        {
            if ("MemFree" == match[1]) {
                memfree = stoul(match[2]);
            } else if ("Buffers" == match[1]) {
                buffers = stoul(match[2]);
            } else if ("Cached" == match[1]) {
                cached = stoul(match[2]);
            } else if ("SReclaimable" == match[1]) {
                sReclaimable = stoul(match[2]);
            } else if ("Shmem" == match[1]) {
                shmem = stoul(match[2]);
            } else if ("SwapFree" == match[1]) {
                availableSwap = stod(match[2]) / (1024 * 1024);
            } else if ("MemTotal" == match[1]) {
                memTotal = stod(match[2]) / (1024 * 1024);
            } else if ("SwapTotal" == match[1]) {
                swapTotal = stod(match[2]) / (1024 * 1024);
            }
        }
        std::getline(meminfo, line);
    }
    if (0 == availableSwap) {
        throw std::runtime_error("Can't get available swap memory");
    }
    availableMem = static_cast<double>(memfree + buffers + cached + sReclaimable - shmem) / (1024 * 1024);
    return {{availableMem, availableSwap}, {memTotal, swapTotal}};
}

MemoryMonitor::MemoryMonitor() :
    enabled{false},
    samplesNumber{0},
    meanMem{0},
    meanSwap{0},
    maxMem{0},
    maxSwap{0},
    memTotal{getMemTotalOnly()},
    swapTotal{getSwapTotalOnly()},
    maxMemTotal{memTotal},
    maxSwapTotal{swapTotal} {}

bool MemoryMonitor::isEnabled() const {
    return enabled;
}

void MemoryMonitor::enable(std::size_t historySize) {
    this->historySize = historySize;
    enabled = true;
}

void MemoryMonitor::disable() {
    enabled = false;
}

void MemoryMonitor::collectData() {
    std::pair<std::pair<double, double>, std::pair<double, double>> availableMemSwapTotalMemSwap
        = getAvailableMemSwapTotalMemSwap();
    memTotal = availableMemSwapTotalMemSwap.second.first;
    swapTotal = availableMemSwapTotalMemSwap.second.second;
    maxMemTotal = std::max(maxMemTotal, memTotal);
    maxSwapTotal = std::max(maxSwapTotal, swapTotal);
    double usedMem = memTotal - availableMemSwapTotalMemSwap.first.first;
    double usedSwap = swapTotal - availableMemSwapTotalMemSwap.first.second;

    if (0 == samplesNumber) {
        maxMem = meanMem = usedMem;
        maxSwap = meanSwap = usedSwap;
        samplesNumber = 1;
    } else {
        meanMem = (meanMem * samplesNumber + usedMem) / (samplesNumber + 1);
        meanSwap = (meanSwap * samplesNumber + usedSwap) / (samplesNumber + 1);
        ++samplesNumber;
        maxMem = std::max(maxMem, usedMem);
        maxSwap = std::max(maxSwap, usedSwap);
    }
    memSwapUsageHistory.emplace_back(usedMem, usedSwap);
    if (memSwapUsageHistory.size() > historySize) {
        memSwapUsageHistory.pop_front();
    }
}

std::deque<std::pair<double, double>> MemoryMonitor::getLastHistory() const {
    return memSwapUsageHistory;
}

double MemoryMonitor::getMeanMem() const {
    return meanMem;
}

double MemoryMonitor::getMeanSwap() const {
    return meanSwap;
}

double MemoryMonitor::getMaxMem() const {
    return maxMem;
}

double MemoryMonitor::getMaxSwap() const {
    return maxSwap;
}

double MemoryMonitor::getMemTotal() const {
    return memTotal;
}

double MemoryMonitor::getSwapTotal() const {
    return swapTotal;
}

double MemoryMonitor::getMaxMemTotal() const {
    return maxMemTotal;
}

double MemoryMonitor::getMaxSwapTotal() const {
    return maxSwapTotal;
}
#endif
