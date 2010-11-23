#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
# A script intended to be useful in helping to collect signatures for a
# release.  This is a pretty rough, and patches are welcome to improve it.
#
# Some thoughts about future improvement:
#  * Store all sigs, etc, in a database
#    - This helps with idempotence
#    - Also allows display of per-file and per-release statistics
#  * A download link on the page itself
#  * Make the textarea access multiple signatures
#  * Don't require the signer to specify which file they are signing
#    - Just check the sign against all the files for that release
#  * Make use of the python-gpg package (http://code.google.com/p/python-gnupg/)
#  * Post to IRC when a new signature is collected
#    - Since we don't want to have a long running bot, perhaps we could
#      also patch wayita to accept and then echo a privmsg?
#


import cgi
import cgitb
cgitb.enable()

import sys, os, string, subprocess, re

try:
  sys.path.append(os.path.dirname(sys.argv[0]))
  import config
except:
  print 'Content-type: text/plain'
  print
  print 'Cannot find config file'
  sys.exit(1)

r = re.compile('\[GNUPG\:\] GOODSIG (\w*) (.*)')

def files():
  for f in os.listdir(config.filesdir):
    if config.version in f and (f.endswith('.tar.gz') or f.endswith('.zip') or f.endswith('.tar.bz2')):
      yield f

shell_content = '''
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
<title>Signature collection for Subversion $version</title>
</head>
<body style="font-size: 14pt; text-align: justify;
  background-color: #f0f0f0; padding: 0 5%">
<p>This page is used to collect signatures for the proposed release of
Apache Subversion $version.</p>
$content
</body>
</html>
'''

def default_page():
  c = '''
<form method="post">
<p>Paste signature in the area below:<br/>
<textarea name="signature" rows="10" cols="80"></textarea>
</p>
<input type="submit" value="Submit" />
</form>
'''

  return c


def save_valid_sig(filename, signature):
  f = open(os.path.join(config.sigdir, filename + '.asc'), 'a')
  f.write(signature)


def verify_sig_for_file(signature, filename):
  args = ['gpg', '--logger-fd', '1', '--no-tty',
          '--status-fd', '2', '--verify', '-',
          os.path.join(config.filesdir, filename)]

  gpg = subprocess.Popen(args,
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)

  gpg.stdin.write(signature)
  gpg.stdin.close()

  rc = gpg.wait()
  output = gpg.stdout.read()
  status = gpg.stderr.read()

  if rc:
    return (False, status + output)

  lines = status.split('\n')
  for line in lines:
    match = r.search(line)
    if match:
      keyid = match.group(1)[-8:]
      user = match.group(2)

  return (True, (filename, keyid, user))

def verify_sig(signature):
  all_failures = ""
  for filename in files():
    (verified, result) = verify_sig_for_file(signature, filename)
    if verified:
      return (verified, result)
    else:
      all_failures += "%s:\n[[[\n%s]]]\n\n" % (filename, result)
  return (False, all_failures)


def process_sig(signature):
  c_verified = '''
  <p style="color: green;">The signature is verified!</p>
  <p>Filename: <code>%s</code></p>
  <p>Key ID: <code>%s</code></p>
  <p>User: <code>%s</code></p>
  <p>This signature has been saved, and will be included as part of the
    release signatures.  Please send mail to
    <a href="mailto:dev@subversion.apache.org">dev@subversion.apache.org</a>
    acknowledging your successful signature.</p>
'''
  c_unverified = '''
  <p style="color: red;">The signature was not able to be verified!</p>
  <p>Signature: <pre>%s</pre></p>
  <p>Reason:</p><pre>%s</pre>
  <p>Please talk to the release manager if this is in error.</p>
'''

  (verified, result) = verify_sig(signature)

  if verified:
    (filename, keyid, user) = result
    save_valid_sig(filename, signature)

    # TODO: record (filename, keyid) in a database
    return c_verified % (filename, keyid, user)
  else:
    return c_unverified % (signature, result)


def main():
  print "Content-Type: text/html"
  print

  form = cgi.FieldStorage()
  if 'signature' not in form:
    content = default_page()
  else:
    content = process_sig(form['signature'].value)

  # These are "global" values, not specific to our action.
  mapping = {
      'version' : config.version,
      'content' : content,
    }

  template = string.Template(shell_content)
  print template.safe_substitute(mapping)


if __name__ == '__main__':
  main()
