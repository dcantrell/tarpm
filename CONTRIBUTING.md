Source Code Control Notes
=========================


Signed-off-by
-------------

Using

```sh
git commit -s
```

to sign-off on commits is preferred, but not required.  See
https://developercertificate.org/ for more information.


Short Log Headers
-----------------

To categorize commits and make release log generation easier, please
use categorization headers on the first line of git commit messages.
The headers the project uses are:

| Prefix | Description |
| ------ | ----------- |
| [bld]  | General release and build process changes |
| [tst]  | Test suite commits |
| [fix]  | General bug fix |
| [new]  | New feature or significant change (not bug fixes) |
| [cmd]  | tarpm(1) command line changes or improvements |
| [ci]   | Changes to the continuous integration scripts and files |
| [doc]  | Documentation changes |

This list may expand over time.

NOTE: Short log messages without a header like this will be excluded
from release announcements.  That may be appropriate for some commits.


Consolidated Project History
----------------------------

Sending pull requests is the preferred workflow, which means
contributors need to track the upstream repo in their forked copies.
Please avoid merge commits as you update your forks so that the commit
history in the main project is consolidated.  An easy way to do that
is:

```sh
git checkout main
git remote add upstream https://github.com/dcantrell/tarpm
git fetch upstream
git rebase upstream/main
git push -f
```

This will track upstream and rebase your copy to the upstream copy.
The force push is required to your copy since you are rewriting the
project history to match upstream.  You need to do this on a clean
repo, so stash anything you are working on and ensure your copy is
clean.


AI Policy
---------

As a policy, the tarpm project does not accept use of generative AI in
contributions.

I feel some clarification on this is in order.  Many developers are
using AI tools to help break apart core dumps or in other debugging
ways.  In that case I view the use of the tool as a tool.  What I'm
not interested in is the automated code generation that you have no
idea how it works or what it's doing, but you got it to compile.
