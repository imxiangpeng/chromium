# Copyright (C) 2011 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#    * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import urllib2


class MockWeb(object):

    def __init__(self, urls=None, responses=None):
        self.urls = urls or {}
        self.urls_fetched = []
        self.responses = responses or []

    def get_binary(self, url, return_none_on_404=False):
        self.urls_fetched.append(url)
        if url in self.urls:
            return self.urls[url]
        return 'MOCK Web result, convert 404 to None=%s' % return_none_on_404

    def request(self, method, url, data, headers=None):  # pylint: disable=unused-argument
        return MockResponse(self.responses.pop(0))


class MockResponse(object):

    def __init__(self, values):
        self.status_code = values['status_code']
        self.url = ''
        self.body = values.get('body', '')

        if int(self.status_code) >= 400:
            raise urllib2.HTTPError(
                url=self.url,
                code=self.status_code,
                msg='Received error status code: {}'.format(self.status_code),
                hdrs={},
                fp=None)

    def getcode(self):
        return self.status_code

    def read(self):
        return self.body
