#include "qraw/diag/logging.h"

namespace qraw::diag {

Q_LOGGING_CATEGORY(lcApp, "qraw.app", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAppLog, "qraw.app.log", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAppCrash, "qraw.app.crash", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAppSession, "qraw.app.session", QtInfoMsg)

} // namespace qraw::diag
