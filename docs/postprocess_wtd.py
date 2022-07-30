# Lint as: python3
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Process the files from Sphinx to optimize the HTML for Coral website."""

import argparse
import os
import re

from bs4 import BeautifulSoup

def lazy_tflm_cleanup(soup):
  """Brute-force removal of stuff we don't want from tflite-micro headers."""
  newsoup = soup.select('section[id="micro-interpreter"] > dl > dd > p')
  for p in newsoup:
    p.extract()
  return soup


def process(file):
  """Runs all the cleanup functions."""
  print('Post-processing ' + file)
  soup = BeautifulSoup(open(file), 'html.parser')
  if os.path.split(file)[1] == 'tensorflow.html':
    soup = lazy_tflm_cleanup(soup)
  with open(file, 'w') as output:
    output.write(str(soup))


def main():
  parser = argparse.ArgumentParser(
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument(
      '-f', '--file', required=True, help='File path of HTML file(s).')
  args = parser.parse_args()

  # Accept a directory or single file
  if os.path.isdir(args.file):
    for file in os.listdir(args.file):
      if os.path.splitext(file)[1] == '.html':
        process(os.path.join(args.file, file))
  else:
    process(args.file)


if __name__ == '__main__':
  main()
