# Author: David Goodger
# Contact: goodger@users.sourceforge.net
# Revision: $Revision: 21817 $
# Date: $Date: 2005-07-21 13:39:57 -0700 (Thu, 21 Jul 2005) $
# Copyright: This module has been placed in the public domain.

# Internationalization details are documented in
# <http://docutils.sf.net/docs/howto/i18n.html>.

"""
This package contains modules for language-dependent features of
reStructuredText.
"""

__docformat__ = 'reStructuredText'

_languages = {}

def get_language(language_code):
    if _languages.has_key(language_code):
        return _languages[language_code]
    try:
        module = __import__(language_code, globals(), locals())
    except ImportError:
        return None
    _languages[language_code] = module
    return module
