#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from bs4 import BeautifulSoup
import requests
import re

req = requests.get('https://docs.python.org/3/library/index.html')
tree = BeautifulSoup(req.text, 'html.parser')

passed_first = False
ignore = re.compile(r'(?:^import\b|with\b|^_|[.]|\(\)$)')

with open('modules/python/_uprocd_modules.py', 'w') as fp:
    for node in tree.select('a.reference.internal code.literal span.pre'):
        module = node.contents[0]
        if module == 'string':
            passed_first = True
        elif not passed_first or ignore.search(module) or module == 'import':
            continue

        fp.write('''
try:
    import {module}
except Exception as ex:
    print('{module}', ex)
'''.format(module=module))
