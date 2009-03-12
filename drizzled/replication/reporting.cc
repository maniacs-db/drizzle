#include <drizzled/server_includes.h>
#include <drizzled/replication/reporting.h>
#include <drizzled/gettext.h>

void
Slave_reporting_capability::report(loglevel level, int err_code,
                                   const char *msg, ...) const
{
  void (*report_function)(const char *, ...);
  char buff[MAX_SLAVE_ERRMSG];
  char *pbuff= buff;
  uint32_t pbuffsize= sizeof(buff);
  va_list args;
  va_start(args, msg);

  switch (level)
  {
  case ERROR_LEVEL:
    /*
      It's an error, it must be reported in Last_error and Last_errno in SHOW
      SLAVE STATUS.
    */
    pbuff= m_last_error.message;
    pbuffsize= sizeof(m_last_error.message);
    m_last_error.number = err_code;
    report_function= sql_print_error;
    break;
  case WARNING_LEVEL:
    report_function= sql_print_warning;
    break;
  case INFORMATION_LEVEL:
    report_function= sql_print_information;
    break;
  default:
    assert(0);                            // should not come here
    return;          // don't crash production builds, just do nothing
  }

  vsnprintf(pbuff, pbuffsize, msg, args);

  va_end(args);

  /* If the msg string ends with '.', do not add a ',' it would be ugly */
  report_function(_("Slave %s: %s%s Error_code: %d"),
                  m_thread_name, pbuff,
                  (pbuff[0] && *(strchr(pbuff, '\0')-1) == '.') ? "" : ",",
                  err_code);
}