# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry import benchmark

from measurements import image_decoding
import page_sets


class ImageDecodingToughImageCases(benchmark.Benchmark):
  test = image_decoding.ImageDecoding
  # TODO: Rename this page set to tough_image_cases.py
  page_set = page_sets.ImageDecodingMeasurementPageSet
