# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import itertools
import json

from telemetry.internal.results import output_formatter
from telemetry.value import summary as summary_module
from telemetry.value import trace

def ResultsAsChartDict(benchmark_metadata, page_specific_values,
                       summary_values):
  """Produces a dict for serialization to Chart JSON format from raw values.

  Chart JSON is a transformation of the basic Telemetry JSON format that
  removes the page map, summarizes the raw values, and organizes the results
  by chart and trace name. This function takes the key pieces of data needed to
  perform this transformation (namely, lists of values and a benchmark metadata
  object) and processes them into a dict which can be serialized using the json
  module.

  Design doc for schema: http://goo.gl/kOtf1Y

  Args:
    page_specific_values: list of page-specific values
    summary_values: list of summary values
    benchmark_metadata: a benchmark.BenchmarkMetadata object

  Returns:
    A Chart JSON dict corresponding to the given data.
  """
  summary = summary_module.Summary(page_specific_values)
  values = itertools.chain(
      summary.interleaved_computed_per_page_values_and_summaries,
      summary_values)
  charts = collections.defaultdict(dict)

  for value in values:
    if value.page:
      chart_name, trace_name = (value.GetChartAndTraceNameForPerPageResult())
    else:
      chart_name, trace_name = (
          value.GetChartAndTraceNameForComputedSummaryResult(None))
      if chart_name == trace_name:
        trace_name = 'summary'

    # Dashboard handles the chart_name of trace values specially: it
    # strips out the field with chart_name 'trace'. Hence in case trace
    # value has tir_label, we preserve the chart_name.
    # For relevant section code of dashboard code that handles this, see:
    # https://github.com/catapult-project/catapult/blob/25e660b/dashboard/dashboard/add_point.py#L199#L216
    if value.tir_label and not isinstance(value, trace.TraceValue):
      chart_name = value.tir_label + '@@' + chart_name

    # This intentionally overwrites the trace if it already exists because this
    # is expected of output from the buildbots currently.
    # See: crbug.com/413393
    charts[chart_name][trace_name] = value.AsDict()

  result_dict = {
    'format_version': '0.1',
    'next_version': '0.2',
    # TODO(sullivan): benchmark_name, benchmark_description, and
    # trace_rerun_options should be removed when incrementing format_version
    # to 0.1.
    'benchmark_name': benchmark_metadata.name,
    'benchmark_description': benchmark_metadata.description,
    'trace_rerun_options': benchmark_metadata.rerun_options,
    'benchmark_metadata': benchmark_metadata.AsDict(),
    'charts': charts,
  }

  return result_dict

# TODO(eakuefner): Transition this to translate Telemetry JSON.
class ChartJsonOutputFormatter(output_formatter.OutputFormatter):
  def __init__(self, output_stream, benchmark_metadata):
    super(ChartJsonOutputFormatter, self).__init__(output_stream)
    self._benchmark_metadata = benchmark_metadata

  def Format(self, page_test_results):
    json.dump(ResultsAsChartDict(
        self._benchmark_metadata,
        page_test_results.all_page_specific_values,
        page_test_results.all_summary_values),
              self.output_stream, indent=2)
    self.output_stream.write('\n')
