# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re

from core import perf_benchmark

from telemetry import benchmark
from telemetry.timeline import chrome_trace_category_filter
from telemetry.timeline import chrome_trace_config
from telemetry.web_perf import timeline_based_measurement

import page_sets


# See tr.v.Numeric.getSummarizedScalarNumericsWithNames()
# https://github.com/catapult-project/catapult/blob/master/tracing/tracing/value/numeric.html#L323
_IGNORED_STATS_RE = re.compile(
    r'(?<!dump)(?<!process)_(std|count|max|min|sum|pct_\d{4}(_\d+)?)$')


class _MemoryInfra(perf_benchmark.PerfBenchmark):
  """Base class for new-generation memory benchmarks based on memory-infra.

  This benchmark records data using memory-infra (https://goo.gl/8tGc6O), which
  is part of chrome tracing, and extracts it using timeline-based measurements.
  """

  def CreateCoreTimelineBasedMeasurementOptions(self):
    # Enable only memory-infra, to get memory dumps, and blink.console, to get
    # the timeline markers used for mapping threads to tabs.
    trace_memory = chrome_trace_category_filter.ChromeTraceCategoryFilter(
        filter_string='-*,blink.console,disabled-by-default-memory-infra')
    tbm_options = timeline_based_measurement.Options(
        overhead_level=trace_memory)
    tbm_options.config.enable_android_graphics_memtrack = True
    tbm_options.SetTimelineBasedMetrics(['memoryMetric'])
    # Setting an empty memory dump config disables periodic dumps.
    tbm_options.config.chrome_trace_config.SetMemoryDumpConfig(
        chrome_trace_config.MemoryDumpConfig())
    return tbm_options

  def SetExtraBrowserOptions(self, options):
    # Just before we measure memory we flush the system caches
    # unfortunately this doesn't immediately take effect, instead
    # the next page run is effected. Due to this the first page run
    # has anomalous results. This option causes us to flush caches
    # each time before Chrome starts so we effect even the first page
    # - avoiding the bug.
    options.clear_sytem_cache_for_browser_and_profile_on_start = True


@benchmark.Enabled('mac')
@benchmark.Enabled('win')
@benchmark.Disabled('android')
@benchmark.Owner(emails=['erikchen@chromium.org'])
class MemoryBenchmarkTrivialSitesDesktop(_MemoryInfra):
  """Measure memory usage on trivial sites."""
  options = {'pageset_repeat': 5}

  def CreateStorySet(self, options):
    return page_sets.TrivialSitesStorySet(wait_in_seconds=0,
                                          measure_memory=True)

  def SetExtraBrowserOptions(self, options):
    super(MemoryBenchmarkTrivialSitesDesktop, self).SetExtraBrowserOptions(
          options)
    options.AppendExtraBrowserArgs([
      '--enable-heap-profiling=native',
    ])

  @classmethod
  def Name(cls):
    return 'memory.desktop'

  @classmethod
  def ValueCanBeAddedPredicate(cls, value, is_first_result):
    # TODO(crbug.com/610962): Remove this stopgap when the perf dashboard
    # is able to cope with the data load generated by TBMv2 metrics.
    return not _IGNORED_STATS_RE.search(value.name)

  def GetExpectations(self):
    return page_sets.TrivialSitesMemoryStoryExpectations()


@benchmark.Enabled('android')  # catapult:#3176
@benchmark.Owner(emails=['perezju@chromium.org'])
class MemoryBenchmarkTop10Mobile(_MemoryInfra):
  """Measure foreground/background memory on top 10 mobile page set.

  This metric provides memory measurements for the System Health Plan of
  Chrome on Android.
  """
  page_set = page_sets.MemoryTop10Mobile
  options = {'pageset_repeat': 5}

  @classmethod
  def Name(cls):
    return 'memory.top_10_mobile'

  @classmethod
  def ShouldTearDownStateAfterEachStoryRun(cls):
    return False

  @classmethod
  def ValueCanBeAddedPredicate(cls, value, is_first_result):
    # TODO(crbug.com/610962): Remove this stopgap when the perf dashboard
    # is able to cope with the data load generated by TBMv2 metrics.
    return not _IGNORED_STATS_RE.search(value.name)

  def GetExpectations(self):
    return page_sets.MemoryTop10MobileStoryExpectations()


class _MemoryV8Benchmark(_MemoryInfra):

  # Report only V8-specific and overall renderer memory values. Note that
  # detailed values reported by the OS (such as native heap) are excluded.
  _V8_AND_OVERALL_MEMORY_RE = re.compile(
      r'renderer_processes:'
      r'(reported_by_chrome:v8|reported_by_os:system_memory:[^:]+$)')

  def CreateCoreTimelineBasedMeasurementOptions(self):
    v8_categories = [
        'blink.console', 'renderer.scheduler', 'v8', 'webkit.console']
    memory_categories = ['blink.console', 'disabled-by-default-memory-infra']
    category_filter = chrome_trace_category_filter.ChromeTraceCategoryFilter(
        ','.join(['-*'] + v8_categories + memory_categories))
    options = timeline_based_measurement.Options(category_filter)
    options.SetTimelineBasedMetrics(['v8AndMemoryMetrics'])
    # Setting an empty memory dump config disables periodic dumps.
    options.config.chrome_trace_config.SetMemoryDumpConfig(
        chrome_trace_config.MemoryDumpConfig())
    return options

  @classmethod
  def ValueCanBeAddedPredicate(cls, value, _):
    if 'memory:chrome' in value.name:
      # TODO(petrcermak): Remove the first two cases once
      # https://codereview.chromium.org/2018503002/ lands in Catapult and rolls
      # into Chromium.
      return ('renderer:subsystem:v8' in value.name or
              'renderer:vmstats:overall' in value.name or
              bool(cls._V8_AND_OVERALL_MEMORY_RE.search(value.name)))
    return 'v8' in value.name


@benchmark.Owner(emails=['ulan@chromium.org'])
class MemoryLongRunningIdleGmail(_MemoryV8Benchmark):
  """Use (recorded) real world web sites and measure memory consumption
  of long running idle Gmail page """
  page_set = page_sets.LongRunningIdleGmailPageSet

  @classmethod
  def Name(cls):
    return 'memory.long_running_idle_gmail_tbmv2'

  @classmethod
  def ShouldDisable(cls, possible_browser):
    return (cls.IsSvelte(possible_browser) or  # http://crbug.com/611167
            # http://crbug.com/671650
            ((possible_browser.browser_type == 'reference' and
              possible_browser.platform.GetDeviceTypeName() == 'Nexus 5')))

  def GetExpectations(self):
    return page_sets.LongRunningIdleGmailStoryExpectations()


@benchmark.Enabled('has tabs')  # http://crbug.com/612210
@benchmark.Owner(emails=['ulan@chromium.org'])
class MemoryLongRunningIdleGmailBackground(_MemoryV8Benchmark):
  """Use (recorded) real world web sites and measure memory consumption
  of long running idle Gmail page """
  page_set = page_sets.LongRunningIdleGmailBackgroundPageSet

  @classmethod
  def Name(cls):
    return 'memory.long_running_idle_gmail_background_tbmv2'

  @classmethod
  def ShouldDisable(cls, possible_browser):  # http://crbug.com/616530
    return cls.IsSvelte(possible_browser)

  def GetExpectations(self):
    return page_sets.LongRunningIdleGmailBackgroundStoryExpectations()
