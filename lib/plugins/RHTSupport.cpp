/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "abrtlib.h"
#include "crash_types.h"
#include "abrt_exception.h"
#include "comm_layer_inner.h"
#include "RHTSupport.h"

using namespace std;

CReporterRHticket::CReporterRHticket()
{
    m_pSettings["URL"] = "https://api.access.redhat.com/rs";
    m_pSettings["Login"] = "";
    m_pSettings["Password"] = "";
    m_pSettings["SSLVerify"] = "yes";
}

CReporterRHticket::~CReporterRHticket()
{
}

void CReporterRHticket::SetSettings(const map_plugin_settings_t& pSettings)
{
    /* Can't simply do this:

    m_pSettings = pSettings;

     * - it will erase keys which aren't present in pSettings.
     * Example: if Bugzilla.conf doesn't have "Login = foo",
     * then there's no pSettings["Login"] and m_pSettings = pSettings
     * will nuke default m_pSettings["Login"] = "",
     * making GUI think that we have no "Login" key at all
     * and thus never overriding it - even if it *has* an override!
     */

    map_plugin_settings_t::iterator it = m_pSettings.begin();
    while (it != m_pSettings.end())
    {
        map_plugin_settings_t::const_iterator override = pSettings.find(it->first);
        if (override != pSettings.end())
        {
            VERB3 log(" rhtsupport settings[%s]='%s'", it->first.c_str(), it->second.c_str());
            it->second = override->second;
        }
        it++;
    }
}

string CReporterRHticket::Report(const map_crash_data_t& crash_data,
                                      const map_plugin_settings_t& settings,
                                      const char *args)
{
    /* abrt-action-bugzilla [-s] -c /etc/arbt/Bugzilla.conf -c - -d pCrashData.dir NULL */
    char *argv[9];
    char **pp = argv;
    *pp++ = (char*)"abrt-action-rhtsupport";

//We want to consume output, so don't redirect to syslog.
//    if (logmode & LOGMODE_SYSLOG)
//        *pp++ = (char*)"-s";
//TODO: the actions<->daemon interaction will be changed anyway...

    *pp++ = (char*)"-c";
    *pp++ = (char*)(PLUGINS_CONF_DIR"/RHTSupport."PLUGINS_CONF_EXTENSION);
    *pp++ = (char*)"-c";
    *pp++ = (char*)"-";
    *pp++ = (char*)"-d";
    *pp++ = (char*)get_crash_data_item_content_or_NULL(crash_data, CD_DUMPDIR);
    *pp = NULL;
    int pipefds[2];
    pid_t pid = fork_execv_on_steroids(EXECFLG_INPUT + EXECFLG_OUTPUT + EXECFLG_ERR2OUT,
                argv,
                pipefds,
                /* unsetenv_vec: */ NULL,
                /* dir: */ NULL,
                /* uid(unused): */ 0
    );

    /* Write the configuration to stdin */
    map_plugin_settings_t::const_iterator it = settings.begin();
    while (it != settings.end())
    {
        full_write_str(pipefds[1], it->first.c_str());
        full_write_str(pipefds[1], "=");
        full_write_str(pipefds[1], it->second.c_str());
        full_write_str(pipefds[1], "\n");
        it++;
    }
    close(pipefds[1]);

    FILE *fp = fdopen(pipefds[0], "r");
    if (!fp)
        die_out_of_memory();

    /* Consume log from stdout */
    std::string bug_status;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp))
    {
        strchrnul(buf, '\n')[0] = '\0';
        if (strncmp(buf, "STATUS:", 7) == 0)
            bug_status = buf + 7;
        else
        if (strncmp(buf, "EXCEPT:", 7) == 0)
        {
            fclose(fp);
            waitpid(pid, NULL, 0);
            throw CABRTException(EXCEP_PLUGIN, "%s", buf + 7);
        }
        else
            update_client("%s", buf);
    }

    fclose(fp); /* this also closes pipefds[0] */
    /* wait for child to actually exit, and prevent leaving a zombie behind */
    waitpid(pid, NULL, 0);

    return bug_status;
}

PLUGIN_INFO(REPORTER,
    CReporterRHticket,
    "RHticket",
    "0.0.4",
    _("Reports bugs to Red Hat support"),
    "Denys Vlasenko <dvlasenk@redhat.com>",
    "https://fedorahosted.org/abrt/wiki",
    PLUGINS_LIB_DIR"/RHTSupport.glade");
