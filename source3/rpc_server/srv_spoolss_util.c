/*
 *  Unix SMB/CIFS implementation.
 *
 *  SPOOLSS RPC Pipe server / winreg client routines
 *
 *  Copyright (c) 2010      Andreas Schneider <asn@samba.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "srv_spoolss_util.h"
#include "../librpc/gen_ndr/srv_winreg.h"
#include "../librpc/gen_ndr/cli_winreg.h"

#define TOP_LEVEL_PRINT_KEY "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Print"
#define TOP_LEVEL_PRINT_PRINTERS_KEY TOP_LEVEL_PRINT_KEY "\\Printers"
#define TOP_LEVEL_CONTROL_KEY "SYSTEM\\CurrentControlSet\\Control\\Print"
#define TOP_LEVEL_CONTROL_FORMS_KEY TOP_LEVEL_CONTROL_KEY "\\Forms"

/*        FLAGS,                NAME,                              with,   height,   left, top, right, bottom */
static const struct spoolss_FormInfo1 builtin_forms1[] = {
	{ SPOOLSS_FORM_BUILTIN, "Letter",                         {0x34b5c,0x44368}, {0x0,0x0,0x34b5c,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Small",                   {0x34b5c,0x44368}, {0x0,0x0,0x34b5c,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Tabloid",                        {0x44368,0x696b8}, {0x0,0x0,0x44368,0x696b8} },
	{ SPOOLSS_FORM_BUILTIN, "Ledger",                         {0x696b8,0x44368}, {0x0,0x0,0x696b8,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Legal",                          {0x34b5c,0x56d10}, {0x0,0x0,0x34b5c,0x56d10} },
	{ SPOOLSS_FORM_BUILTIN, "Statement",                      {0x221b4,0x34b5c}, {0x0,0x0,0x221b4,0x34b5c} },
	{ SPOOLSS_FORM_BUILTIN, "Executive",                      {0x2cf56,0x411cc}, {0x0,0x0,0x2cf56,0x411cc} },
	{ SPOOLSS_FORM_BUILTIN, "A3",                             {0x48828,0x668a0}, {0x0,0x0,0x48828,0x668a0} },
	{ SPOOLSS_FORM_BUILTIN, "A4",                             {0x33450,0x48828}, {0x0,0x0,0x33450,0x48828} },
	{ SPOOLSS_FORM_BUILTIN, "A4 Small",                       {0x33450,0x48828}, {0x0,0x0,0x33450,0x48828} },
	{ SPOOLSS_FORM_BUILTIN, "A5",                             {0x24220,0x33450}, {0x0,0x0,0x24220,0x33450} },
	{ SPOOLSS_FORM_BUILTIN, "B4 (JIS)",                       {0x3ebe8,0x58de0}, {0x0,0x0,0x3ebe8,0x58de0} },
	{ SPOOLSS_FORM_BUILTIN, "B5 (JIS)",                       {0x2c6f0,0x3ebe8}, {0x0,0x0,0x2c6f0,0x3ebe8} },
	{ SPOOLSS_FORM_BUILTIN, "Folio",                          {0x34b5c,0x509d8}, {0x0,0x0,0x34b5c,0x509d8} },
	{ SPOOLSS_FORM_BUILTIN, "Quarto",                         {0x347d8,0x43238}, {0x0,0x0,0x347d8,0x43238} },
	{ SPOOLSS_FORM_BUILTIN, "10x14",                          {0x3e030,0x56d10}, {0x0,0x0,0x3e030,0x56d10} },
	{ SPOOLSS_FORM_BUILTIN, "11x17",                          {0x44368,0x696b8}, {0x0,0x0,0x44368,0x696b8} },
	{ SPOOLSS_FORM_BUILTIN, "Note",                           {0x34b5c,0x44368}, {0x0,0x0,0x34b5c,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope #9",                    {0x18079,0x37091}, {0x0,0x0,0x18079,0x37091} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope #10",                   {0x19947,0x3ae94}, {0x0,0x0,0x19947,0x3ae94} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope #11",                   {0x1be7c,0x40565}, {0x0,0x0,0x1be7c,0x40565} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope #12",                   {0x1d74a,0x44368}, {0x0,0x0,0x1d74a,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope #14",                   {0x1f018,0x47504}, {0x0,0x0,0x1f018,0x47504} },
	{ SPOOLSS_FORM_BUILTIN, "C size sheet",                   {0x696b8,0x886d0}, {0x0,0x0,0x696b8,0x886d0} },
	{ SPOOLSS_FORM_BUILTIN, "D size sheet",                   {0x886d0,0xd2d70}, {0x0,0x0,0x886d0,0xd2d70} },
	{ SPOOLSS_FORM_BUILTIN, "E size sheet",                   {0xd2d70,0x110da0},{0x0,0x0,0xd2d70,0x110da0} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope DL",                    {0x1adb0,0x35b60}, {0x0,0x0,0x1adb0,0x35b60} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope C5",                    {0x278d0,0x37e88}, {0x0,0x0,0x278d0,0x37e88} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope C3",                    {0x4f1a0,0x6fd10}, {0x0,0x0,0x4f1a0,0x6fd10} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope C4",                    {0x37e88,0x4f1a0}, {0x0,0x0,0x37e88,0x4f1a0} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope C6",                    {0x1bd50,0x278d0}, {0x0,0x0,0x1bd50,0x278d0} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope C65",                   {0x1bd50,0x37e88}, {0x0,0x0,0x1bd50,0x37e88} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope B4",                    {0x3d090,0x562e8}, {0x0,0x0,0x3d090,0x562e8} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope B5",                    {0x2af80,0x3d090}, {0x0,0x0,0x2af80,0x3d090} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope B6",                    {0x2af80,0x1e848}, {0x0,0x0,0x2af80,0x1e848} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope",                       {0x1adb0,0x38270}, {0x0,0x0,0x1adb0,0x38270} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope Monarch",               {0x18079,0x2e824}, {0x0,0x0,0x18079,0x2e824} },
	{ SPOOLSS_FORM_BUILTIN, "6 3/4 Envelope",                 {0x167ab,0x284ec}, {0x0,0x0,0x167ab,0x284ec} },
	{ SPOOLSS_FORM_BUILTIN, "US Std Fanfold",                 {0x5c3e1,0x44368}, {0x0,0x0,0x5c3e1,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "German Std Fanfold",             {0x34b5c,0x4a6a0}, {0x0,0x0,0x34b5c,0x4a6a0} },
	{ SPOOLSS_FORM_BUILTIN, "German Legal Fanfold",           {0x34b5c,0x509d8}, {0x0,0x0,0x34b5c,0x509d8} },
	{ SPOOLSS_FORM_BUILTIN, "B4 (ISO)",                       {0x3d090,0x562e8}, {0x0,0x0,0x3d090,0x562e8} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Postcard",              {0x186a0,0x24220}, {0x0,0x0,0x186a0,0x24220} },
	{ SPOOLSS_FORM_BUILTIN, "9x11",                           {0x37cf8,0x44368}, {0x0,0x0,0x37cf8,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "10x11",                          {0x3e030,0x44368}, {0x0,0x0,0x3e030,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "15x11",                          {0x5d048,0x44368}, {0x0,0x0,0x5d048,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "Envelope Invite",                {0x35b60,0x35b60}, {0x0,0x0,0x35b60,0x35b60} },
	{ SPOOLSS_FORM_BUILTIN, "Reserved48",                     {0x1,0x1},         {0x0,0x0,0x1,0x1} },
	{ SPOOLSS_FORM_BUILTIN, "Reserved49",                     {0x1,0x1},         {0x0,0x0,0x1,0x1} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Extra",                   {0x3ae94,0x4a6a0}, {0x0,0x0,0x3ae94,0x4a6a0} },
	{ SPOOLSS_FORM_BUILTIN, "Legal Extra",                    {0x3ae94,0x5d048}, {0x0,0x0,0x3ae94,0x5d048} },
	{ SPOOLSS_FORM_BUILTIN, "Tabloid Extra",                  {0x4a6a0,0x6f9f0}, {0x0,0x0,0x4a6a0,0x6f9f0} },
	{ SPOOLSS_FORM_BUILTIN, "A4 Extra",                       {0x397c2,0x4eb16}, {0x0,0x0,0x397c2,0x4eb16} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Transverse",              {0x34b5c,0x44368}, {0x0,0x0,0x34b5c,0x44368} },
	{ SPOOLSS_FORM_BUILTIN, "A4 Transverse",                  {0x33450,0x48828}, {0x0,0x0,0x33450,0x48828} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Extra Transverse",        {0x3ae94,0x4a6a0}, {0x0,0x0,0x3ae94,0x4a6a0} },
	{ SPOOLSS_FORM_BUILTIN, "Super A",                        {0x376b8,0x56ea0}, {0x0,0x0,0x376b8,0x56ea0} },
	{ SPOOLSS_FORM_BUILTIN, "Super B",                        {0x4a768,0x76e58}, {0x0,0x0,0x4a768,0x76e58} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Plus",                    {0x34b5c,0x4eb16}, {0x0,0x0,0x34b5c,0x4eb16} },
	{ SPOOLSS_FORM_BUILTIN, "A4 Plus",                        {0x33450,0x50910}, {0x0,0x0,0x33450,0x50910} },
	{ SPOOLSS_FORM_BUILTIN, "A5 Transverse",                  {0x24220,0x33450}, {0x0,0x0,0x24220,0x33450} },
	{ SPOOLSS_FORM_BUILTIN, "B5 (JIS) Transverse",            {0x2c6f0,0x3ebe8}, {0x0,0x0,0x2c6f0,0x3ebe8} },
	{ SPOOLSS_FORM_BUILTIN, "A3 Extra",                       {0x4e9d0,0x6ca48}, {0x0,0x0,0x4e9d0,0x6ca48} },
	{ SPOOLSS_FORM_BUILTIN, "A5 Extra",                       {0x2a7b0,0x395f8}, {0x0,0x0,0x2a7b0,0x395f8} },
	{ SPOOLSS_FORM_BUILTIN, "B5 (ISO) Extra",                 {0x31128,0x43620}, {0x0,0x0,0x31128,0x43620} },
	{ SPOOLSS_FORM_BUILTIN, "A2",                             {0x668a0,0x91050}, {0x0,0x0,0x668a0,0x91050} },
	{ SPOOLSS_FORM_BUILTIN, "A3 Transverse",                  {0x48828,0x668a0}, {0x0,0x0,0x48828,0x668a0} },
	{ SPOOLSS_FORM_BUILTIN, "A3 Extra Transverse",            {0x4e9d0,0x6ca48}, {0x0,0x0,0x4e9d0,0x6ca48} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Double Postcard",       {0x30d40,0x24220}, {0x0,0x0,0x30d40,0x24220} },
	{ SPOOLSS_FORM_BUILTIN, "A6",                             {0x19a28,0x24220}, {0x0,0x0,0x19a28,0x24220} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Envelope Kaku #2",      {0x3a980,0x510e0}, {0x0,0x0,0x3a980,0x510e0} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Envelope Kaku #3",      {0x34bc0,0x43a08}, {0x0,0x0,0x34bc0,0x43a08} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Envelope Chou #3",      {0x1d4c0,0x395f8}, {0x0,0x0,0x1d4c0,0x395f8} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Envelope Chou #4",      {0x15f90,0x320c8}, {0x0,0x0,0x15f90,0x320c8} },
	{ SPOOLSS_FORM_BUILTIN, "Letter Rotated",                 {0x44368,0x34b5c}, {0x0,0x0,0x44368,0x34b5c} },
	{ SPOOLSS_FORM_BUILTIN, "A3 Rotated",                     {0x668a0,0x48828}, {0x0,0x0,0x668a0,0x48828} },
	{ SPOOLSS_FORM_BUILTIN, "A4 Rotated",                     {0x48828,0x33450}, {0x0,0x0,0x48828,0x33450} },
	{ SPOOLSS_FORM_BUILTIN, "A5 Rotated",                     {0x33450,0x24220}, {0x0,0x0,0x33450,0x24220} },
	{ SPOOLSS_FORM_BUILTIN, "B4 (JIS) Rotated",               {0x58de0,0x3ebe8}, {0x0,0x0,0x58de0,0x3ebe8} },
	{ SPOOLSS_FORM_BUILTIN, "B5 (JIS) Rotated",               {0x3ebe8,0x2c6f0}, {0x0,0x0,0x3ebe8,0x2c6f0} },
	{ SPOOLSS_FORM_BUILTIN, "Japanese Postcard Rotated",      {0x24220,0x186a0}, {0x0,0x0,0x24220,0x186a0} },
	{ SPOOLSS_FORM_BUILTIN, "Double Japan Postcard Rotated",  {0x24220,0x30d40}, {0x0,0x0,0x24220,0x30d40} },
	{ SPOOLSS_FORM_BUILTIN, "A6 Rotated",                     {0x24220,0x19a28}, {0x0,0x0,0x24220,0x19a28} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope Kaku #2 Rotated", {0x510e0,0x3a980}, {0x0,0x0,0x510e0,0x3a980} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope Kaku #3 Rotated", {0x43a08,0x34bc0}, {0x0,0x0,0x43a08,0x34bc0} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope Chou #3 Rotated", {0x395f8,0x1d4c0}, {0x0,0x0,0x395f8,0x1d4c0} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope Chou #4 Rotated", {0x320c8,0x15f90}, {0x0,0x0,0x320c8,0x15f90} },
	{ SPOOLSS_FORM_BUILTIN, "B6 (JIS)",                       {0x1f400,0x2c6f0}, {0x0,0x0,0x1f400,0x2c6f0} },
	{ SPOOLSS_FORM_BUILTIN, "B6 (JIS) Rotated",               {0x2c6f0,0x1f400}, {0x0,0x0,0x2c6f0,0x1f400} },
	{ SPOOLSS_FORM_BUILTIN, "12x11",                          {0x4a724,0x443e1}, {0x0,0x0,0x4a724,0x443e1} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope You #4",          {0x19a28,0x395f8}, {0x0,0x0,0x19a28,0x395f8} },
	{ SPOOLSS_FORM_BUILTIN, "Japan Envelope You #4 Rotated",  {0x395f8,0x19a28}, {0x0,0x0,0x395f8,0x19a28} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 16K",                        {0x2de60,0x3f7a0}, {0x0,0x0,0x2de60,0x3f7a0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 32K",                        {0x1fbd0,0x2cec0}, {0x0,0x0,0x1fbd0,0x2cec0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 32K(Big)",                   {0x222e0,0x318f8}, {0x0,0x0,0x222e0,0x318f8} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #1",                {0x18e70,0x28488}, {0x0,0x0,0x18e70,0x28488} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #2",                {0x18e70,0x2af80}, {0x0,0x0,0x18e70,0x2af80} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #3",                {0x1e848,0x2af80}, {0x0,0x0,0x1e848,0x2af80} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #4",                {0x1adb0,0x32c80}, {0x0,0x0,0x1adb0,0x32c80} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #5",                {0x1adb0,0x35b60}, {0x0,0x0,0x1adb0,0x35b60} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #6",                {0x1d4c0,0x38270}, {0x0,0x0,0x1d4c0,0x38270} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #7",                {0x27100,0x38270}, {0x0,0x0,0x27100,0x38270} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #8",                {0x1d4c0,0x4b708}, {0x0,0x0,0x1d4c0,0x4b708} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #9",                {0x37e88,0x4f1a0}, {0x0,0x0,0x37e88,0x4f1a0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #10",               {0x4f1a0,0x6fd10}, {0x0,0x0,0x4f1a0,0x6fd10} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 16K Rotated",                {0x3f7a0,0x2de60}, {0x0,0x0,0x3f7a0,0x2de60} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 32K Rotated",                {0x2cec0,0x1fbd0}, {0x0,0x0,0x2cec0,0x1fbd0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC 32K(Big) Rotated",           {0x318f8,0x222e0}, {0x0,0x0,0x318f8,0x222e0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #1 Rotated",        {0x28488,0x18e70}, {0x0,0x0,0x28488,0x18e70} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #2 Rotated",        {0x2af80,0x18e70}, {0x0,0x0,0x2af80,0x18e70} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #3 Rotated",        {0x2af80,0x1e848}, {0x0,0x0,0x2af80,0x1e848} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #4 Rotated",        {0x32c80,0x1adb0}, {0x0,0x0,0x32c80,0x1adb0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #5 Rotated",        {0x35b60,0x1adb0}, {0x0,0x0,0x35b60,0x1adb0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #6 Rotated",        {0x38270,0x1d4c0}, {0x0,0x0,0x38270,0x1d4c0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #7 Rotated",        {0x38270,0x27100}, {0x0,0x0,0x38270,0x27100} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #8 Rotated",        {0x4b708,0x1d4c0}, {0x0,0x0,0x4b708,0x1d4c0} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #9 Rotated",        {0x4f1a0,0x37e88}, {0x0,0x0,0x4f1a0,0x37e88} },
	{ SPOOLSS_FORM_BUILTIN, "PRC Envelope #10 Rotated",       {0x6fd10,0x4f1a0}, {0x0,0x0,0x6fd10,0x4f1a0} }
};

/********************************************************************
 static helper functions
********************************************************************/

/****************************************************************************
 Update the changeid time.
****************************************************************************/
/**
 * @internal
 *
 * @brief Update the ChangeID time of a printer.
 *
 * This is SO NASTY as some drivers need this to change, others need it
 * static. This value will change every second, and I must hope that this
 * is enough..... DON'T CHANGE THIS CODE WITHOUT A TEST MATRIX THE SIZE OF
 * UTAH ! JRA.
 *
 * @return              The ChangeID.
 */
static uint32_t winreg_printer_rev_changeid(void)
{
	struct timeval tv;

	get_process_uptime(&tv);

#if 1	/* JERRY */
	/* Return changeid as msec since spooler restart */
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#else
	/*
	 * This setting seems to work well but is too untested
	 * to replace the above calculation.  Left in for experiementation
	 * of the reader            --jerry (Tue Mar 12 09:15:05 CST 2002)
	 */
	return tv.tv_sec * 10 + tv.tv_usec / 100000;
#endif
}

/**
 * @internal
 *
 * @brief Connect to the interal winreg server and open the given printer key.
 *
 * The function will create the needed subkeys if they don't exist.
 *
 * @param[in]  mem_ctx       The memory context to use.
 *
 * @param[in]  server_info   The supplied server info.
 *
 * @param[out] winreg_pipe   A pointer for the winreg rpc client pipe.
 *
 * @param[in]  path          The path to the key to open.
 *
 * @param[in]  key           The key to open.
 *
 * @param[in]  create_key    Set to true if the key should be created if it
 *                           doesn't exist.
 *
 * @param[in]  access_mask   The access mask to open the key.
 *
 * @param[out] hive_handle   A policy handle for the opened hive.
 *
 * @param[out] key_handle    A policy handle for the opened key.
 *
 * @return                   WERR_OK on success, the corresponding DOS error
 *                           code if something gone wrong.
 */
static WERROR winreg_printer_openkey(TALLOC_CTX *mem_ctx,
			      struct auth_serversupplied_info *server_info,
			      struct rpc_pipe_client **winreg_pipe,
			      const char *path,
			      const char *key,
			      bool create_key,
			      uint32_t access_mask,
			      struct policy_handle *hive_handle,
			      struct policy_handle *key_handle)
{
	struct rpc_pipe_client *pipe_handle;
	struct winreg_String wkey, wkeyclass;
	char *keyname;
	NTSTATUS status;
	WERROR result = WERR_OK;

	/* create winreg connection */
	status = rpc_pipe_open_internal(mem_ctx,
					&ndr_table_winreg.syntax_id,
					rpc_winreg_dispatch,
					server_info,
					&pipe_handle);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_openkey: Could not connect to winreg_pipe: %s\n",
			  nt_errstr(status)));
		return ntstatus_to_werror(status);
	}

	status = rpccli_winreg_OpenHKLM(pipe_handle,
					mem_ctx,
					NULL,
					access_mask,
					hive_handle,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_openkey: Could not open HKLM hive: %s\n",
			  nt_errstr(status)));
		talloc_free(pipe_handle);
		if (!W_ERROR_IS_OK(result)) {
			return result;
		}
		return ntstatus_to_werror(status);
	}

	if (key && *key) {
		keyname = talloc_asprintf(mem_ctx, "%s\\%s", path, key);
	} else {
		keyname = talloc_strdup(mem_ctx, path);
	}
	if (keyname == NULL) {
		talloc_free(pipe_handle);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(wkey);
	wkey.name = keyname;

	if (create_key) {
		enum winreg_CreateAction action = REG_ACTION_NONE;

		ZERO_STRUCT(wkeyclass);
		wkeyclass.name = "";

		status = rpccli_winreg_CreateKey(pipe_handle,
						 mem_ctx,
						 hive_handle,
						 wkey,
						 wkeyclass,
						 0,
						 access_mask,
						 NULL,
						 key_handle,
						 &action,
						 &result);
		switch (action) {
			case REG_ACTION_NONE:
				DEBUG(8, ("winreg_printer_openkey:createkey did nothing -- huh?\n"));
				break;
			case REG_CREATED_NEW_KEY:
				DEBUG(8, ("winreg_printer_openkey: createkey created %s\n", keyname));
				break;
			case REG_OPENED_EXISTING_KEY:
				DEBUG(8, ("winreg_printer_openkey: createkey opened existing %s\n", keyname));
				break;
		}
	} else {
		status = rpccli_winreg_OpenKey(pipe_handle,
					       mem_ctx,
					       hive_handle,
					       wkey,
					       0,
					       access_mask,
					       key_handle,
					       &result);
	}
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(pipe_handle);
		if (!W_ERROR_IS_OK(result)) {
			return result;
		}
		return ntstatus_to_werror(status);
	}

	*winreg_pipe = pipe_handle;

	return WERR_OK;
}

/**
 * @brief Create the registry keyname for the given printer.
 *
 * @param[in]  mem_ctx  The memory context to use.
 *
 * @param[in]  printer  The name of the printer to get the registry key.
 *
 * @return     The registry key or NULL on error.
 */
static char *winreg_printer_data_keyname(TALLOC_CTX *mem_ctx, const char *printer) {
	return talloc_asprintf(mem_ctx, "%s\\%s", TOP_LEVEL_PRINT_PRINTERS_KEY, printer);
}

/**
 * @internal
 *
 * @brief Enumerate values of an opened key handle and retrieve the data.
 *
 * @param[in]  mem_ctx  The memory context to use.
 *
 * @param[in]  pipe_handle The pipe handle for the rpc connection.
 *
 * @param[in]  key_hnd  The opened key handle.
 *
 * @param[out] pnum_values A pointer to store he number of values found.
 *
 * @param[out] pnum_values A pointer to store the number of values we found.
 *
 * @return                   WERR_OK on success, the corresponding DOS error
 *                           code if something gone wrong.
 */
static WERROR winreg_printer_enumvalues(TALLOC_CTX *mem_ctx,
					struct rpc_pipe_client *pipe_handle,
					struct policy_handle *key_hnd,
					uint32_t *pnum_values,
					struct spoolss_PrinterEnumValues **penum_values)
{
	TALLOC_CTX *tmp_ctx;
	uint32_t num_subkeys, max_subkeylen, max_classlen;
	uint32_t num_values, max_valnamelen, max_valbufsize;
	uint32_t secdescsize;
	uint32_t i;
	NTTIME last_changed_time;
	struct winreg_String classname;

	struct spoolss_PrinterEnumValues *enum_values;

	WERROR result = WERR_OK;
	NTSTATUS status;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(classname);

	status = rpccli_winreg_QueryInfoKey(pipe_handle,
					    tmp_ctx,
					    key_hnd,
					    &classname,
					    &num_subkeys,
					    &max_subkeylen,
					    &max_classlen,
					    &num_values,
					    &max_valnamelen,
					    &max_valbufsize,
					    &secdescsize,
					    &last_changed_time,
					    &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_enumvalues: Could not query info: %s\n",
			  nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto error;
		}
		result = ntstatus_to_werror(status);
		goto error;
	}

	if (num_values == 0) {
		*pnum_values = 0;
		TALLOC_FREE(tmp_ctx);
		return WERR_OK;
	}

	enum_values = TALLOC_ARRAY(tmp_ctx, struct spoolss_PrinterEnumValues, num_values);
	if (enum_values == NULL) {
		result = WERR_NOMEM;
		goto error;
	}

	for (i = 0; i < num_values; i++) {
		struct spoolss_PrinterEnumValues val;
		struct winreg_ValNameBuf name_buf;
		enum winreg_Type type = REG_NONE;
		uint8_t *data = NULL;
		uint32_t data_size;
		uint32_t length;
		char n = '\0';;

		name_buf.name = &n;
		name_buf.size = max_valnamelen + 2;
		name_buf.length = 0;

		data_size = max_valbufsize;
		data = (uint8_t *) TALLOC(tmp_ctx, data_size);
		length = 0;

		status = rpccli_winreg_EnumValue(pipe_handle,
						 tmp_ctx,
						 key_hnd,
						 i,
						 &name_buf,
						 &type,
						 data,
						 &data_size,
						 &length,
						 &result);
		if (W_ERROR_EQUAL(result, WERR_NO_MORE_ITEMS) ) {
			result = WERR_OK;
			status = NT_STATUS_OK;
			break;
		}

		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(0, ("winreg_printer_enumvalues: Could not enumerate values: %s\n",
				  nt_errstr(status)));
			if (!W_ERROR_IS_OK(result)) {
				goto error;
			}
			result = ntstatus_to_werror(status);
			goto error;
		}

		if (name_buf.name == NULL) {
			result = WERR_INVALID_PARAMETER;
			goto error;
		}

		val.value_name = talloc_strdup(enum_values, name_buf.name);
		if (val.value_name == NULL) {
			result = WERR_NOMEM;
			goto error;
		}
		val.value_name_len = strlen_m_term(val.value_name) * 2;

		val.type = type;
		val.data_length = data_size;
		val.data = NULL;
		if (val.data_length) {
			val.data = talloc(enum_values, DATA_BLOB);
			if (val.data == NULL) {
				result = WERR_NOMEM;
				goto error;
			}
			*val.data = data_blob_talloc(enum_values, data, data_size);
		}

		enum_values[i] = val;
	}

	*pnum_values = num_values;
	if (penum_values) {
		*penum_values = talloc_move(mem_ctx, &enum_values);
	}

	result = WERR_OK;

 error:
	TALLOC_FREE(tmp_ctx);
	return result;
}

/**
 * @internal
 *
 * @brief Enumerate subkeys of an opened key handle and get the names.
 *
 * @param[in]  mem_ctx  The memory context to use.
 *
 * @param[in]  pipe_handle The pipe handle for the rpc connection.
 *
 * @param[in]  key_hnd  The opened key handle.
 *
 * @param[in]  pnum_subkeys A pointer to store the number of found subkeys.
 *
 * @param[in]  psubkeys A pointer to an array to store the found names of
 *                      subkeys.
 *
 * @return                   WERR_OK on success, the corresponding DOS error
 *                           code if something gone wrong.
 */
static WERROR winreg_printer_enumkeys(TALLOC_CTX *mem_ctx,
				      struct rpc_pipe_client *pipe_handle,
				      struct policy_handle *key_hnd,
				      uint32_t *pnum_subkeys,
				      const char ***psubkeys)
{
	TALLOC_CTX *tmp_ctx;
	const char **subkeys;
	uint32_t num_subkeys, max_subkeylen, max_classlen;
	uint32_t num_values, max_valnamelen, max_valbufsize;
	uint32_t i;
	NTTIME last_changed_time;
	uint32_t secdescsize;
	struct winreg_String classname;
	WERROR result = WERR_OK;
	NTSTATUS status;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(classname);

	status = rpccli_winreg_QueryInfoKey(pipe_handle,
					    tmp_ctx,
					    key_hnd,
					    &classname,
					    &num_subkeys,
					    &max_subkeylen,
					    &max_classlen,
					    &num_values,
					    &max_valnamelen,
					    &max_valbufsize,
					    &secdescsize,
					    &last_changed_time,
					    &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_enumkeys: Could not query info: %s\n",
			  nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto error;
		}
		result = ntstatus_to_werror(status);
		goto error;
	}

	subkeys = talloc_zero_array(tmp_ctx, const char *, num_subkeys + 2);
	if (subkeys == NULL) {
		result = WERR_NOMEM;
		goto error;
	}

	if (num_subkeys == 0) {
		subkeys[0] = talloc_strdup(subkeys, "");
		if (subkeys[0] == NULL) {
			result = WERR_NOMEM;
			goto error;
		}
		*pnum_subkeys = 0;
		if (psubkeys) {
			*psubkeys = talloc_move(mem_ctx, &subkeys);
		}

		TALLOC_FREE(tmp_ctx);
		return WERR_OK;
	}

	for (i = 0; i < num_subkeys; i++) {
		char c = '\0';
		char n = '\0';
		char *name = NULL;
		struct winreg_StringBuf class_buf;
		struct winreg_StringBuf name_buf;
		NTTIME modtime;

		class_buf.name = &c;
		class_buf.size = max_classlen + 2;
		class_buf.length = 0;

		name_buf.name = &n;
		name_buf.size = max_subkeylen + 2;
		name_buf.length = 0;

		ZERO_STRUCT(modtime);

		status = rpccli_winreg_EnumKey(pipe_handle,
					       tmp_ctx,
					       key_hnd,
					       i,
					       &name_buf,
					       &class_buf,
					       &modtime,
					       &result);
		if (W_ERROR_EQUAL(result, WERR_NO_MORE_ITEMS) ) {
			result = WERR_OK;
			status = NT_STATUS_OK;
			break;
		}

		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(0, ("winreg_printer_enumkeys: Could not enumerate keys: %s\n",
				  nt_errstr(status)));
			if (!W_ERROR_IS_OK(result)) {
				goto error;
			}
			result = ntstatus_to_werror(status);
			goto error;
		}

		if (name_buf.name == NULL) {
			result = WERR_INVALID_PARAMETER;
			goto error;
		}

		name = talloc_strdup(subkeys, name_buf.name);
		if (name == NULL) {
			result = WERR_NOMEM;
			goto error;
		}

		subkeys[i] = name;
	}

	*pnum_subkeys = num_subkeys;
	if (psubkeys) {
		*psubkeys = talloc_move(mem_ctx, &subkeys);
	}

 error:
	TALLOC_FREE(tmp_ctx);
	return result;
}

/**
 * @internal
 *
 * @brief A function to delete a key and its subkeys recurively.
 *
 * @param[in]  mem_ctx  The memory context to use.
 *
 * @param[in]  pipe_handle The pipe handle for the rpc connection.
 *
 * @param[in]  hive_handle A opened hive handle to the key.
 *
 * @param[in]  access_mask The access mask to access the key.
 *
 * @param[in]  key      The key to delete
 *
 * @return              WERR_OK on success, the corresponding DOS error
 *                      code if something gone wrong.
 */
static WERROR winreg_printer_delete_subkeys(TALLOC_CTX *mem_ctx,
					    struct rpc_pipe_client *pipe_handle,
					    struct policy_handle *hive_handle,
					    uint32_t access_mask,
					    const char *key)
{
	const char **subkeys = NULL;
	uint32_t num_subkeys = 0;
	struct policy_handle key_hnd;
	struct winreg_String wkey;
	WERROR result = WERR_OK;
	NTSTATUS status;
	uint32_t i;

	ZERO_STRUCT(key_hnd);
	wkey.name = key;

	DEBUG(2, ("winreg_printer_delete_subkeys: delete key %s\n", key));
	/* open the key */
	status = rpccli_winreg_OpenKey(pipe_handle,
				       mem_ctx,
				       hive_handle,
				       wkey,
				       0,
				       access_mask,
				       &key_hnd,
				       &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_delete_subkeys: Could not open key %s: %s\n",
			  wkey.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			return result;
		}
		return ntstatus_to_werror(status);
	}

	result = winreg_printer_enumkeys(mem_ctx,
					 pipe_handle,
					 &key_hnd,
					 &num_subkeys,
					 &subkeys);
	if (!W_ERROR_IS_OK(result)) {
		goto done;
	}

	for (i = 0; i < num_subkeys; i++) {
		/* create key + subkey */
		char *subkey = talloc_asprintf(mem_ctx, "%s\\%s", key, subkeys[i]);
		if (subkey == NULL) {
			goto done;
		}

		DEBUG(2, ("winreg_printer_delete_subkeys: delete subkey %s\n", subkey));
		result = winreg_printer_delete_subkeys(mem_ctx,
						       pipe_handle,
						       hive_handle,
						       access_mask,
						       subkey);
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
	}

	if (is_valid_policy_hnd(&key_hnd)) {
		rpccli_winreg_CloseKey(pipe_handle, mem_ctx, &key_hnd, NULL);
	}

	wkey.name = key;

	status = rpccli_winreg_DeleteKey(pipe_handle,
					 mem_ctx,
					 hive_handle,
					 wkey,
					 &result);

done:
	if (is_valid_policy_hnd(&key_hnd)) {
		rpccli_winreg_CloseKey(pipe_handle, mem_ctx, &key_hnd, NULL);
	}

	return result;
}

static WERROR winreg_printer_write_sz(TALLOC_CTX *mem_ctx,
				      struct rpc_pipe_client *pipe_handle,
				      struct policy_handle *key_handle,
				      const char *value,
				      const char *data)
{
	struct winreg_String wvalue;
	DATA_BLOB blob;
	WERROR result = WERR_OK;
	NTSTATUS status;

	wvalue.name = value;
	if (data == NULL) {
		blob = data_blob_string_const("");
	} else {
		if (!push_reg_sz(mem_ctx, NULL, &blob, data)) {
			DEBUG(0, ("winreg_printer_write_sz: Could not marshall string %s for %s\n",
				data, wvalue.name));
			return WERR_NOMEM;
		}
	}
	status = rpccli_winreg_SetValue(pipe_handle,
					mem_ctx,
					key_handle,
					wvalue,
					REG_SZ,
					blob.data,
					blob.length,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_write_sz: Could not set value %s: %s\n",
			wvalue.name, win_errstr(result)));
		if (!W_ERROR_IS_OK(result)) {
			result = ntstatus_to_werror(status);
		}
	}

	return result;
}

static WERROR winreg_printer_write_dword(TALLOC_CTX *mem_ctx,
					 struct rpc_pipe_client *pipe_handle,
					 struct policy_handle *key_handle,
					 const char *value,
					 uint32_t data)
{
	struct winreg_String wvalue;
	DATA_BLOB blob;
	WERROR result = WERR_OK;
	NTSTATUS status;

	wvalue.name = value;
	blob = data_blob_talloc(mem_ctx, NULL, 4);
	SIVAL(blob.data, 0, data);

	status = rpccli_winreg_SetValue(pipe_handle,
					mem_ctx,
					key_handle,
					wvalue,
					REG_DWORD,
					blob.data,
					blob.length,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_write_dword: Could not set value %s: %s\n",
			wvalue.name, win_errstr(result)));
		if (!W_ERROR_IS_OK(result)) {
			result = ntstatus_to_werror(status);
		}
	}

	return result;
}

static WERROR winreg_printer_write_binary(TALLOC_CTX *mem_ctx,
					  struct rpc_pipe_client *pipe_handle,
					  struct policy_handle *key_handle,
					  const char *value,
					  DATA_BLOB blob)
{
	struct winreg_String wvalue;
	WERROR result = WERR_OK;
	NTSTATUS status;

	wvalue.name = value;
	status = rpccli_winreg_SetValue(pipe_handle,
					mem_ctx,
					key_handle,
					wvalue,
					REG_BINARY,
					blob.data,
					blob.length,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_write_binary: Could not set value %s: %s\n",
			wvalue.name, win_errstr(result)));
		if (!W_ERROR_IS_OK(result)) {
			result = ntstatus_to_werror(status);
		}
	}

	return result;
}

static WERROR winreg_printer_query_dword(TALLOC_CTX *mem_ctx,
					 struct rpc_pipe_client *pipe_handle,
					 struct policy_handle *key_handle,
					 const char *value,
					 uint32_t *data)
{
	struct winreg_String wvalue;
	enum winreg_Type type;
	WERROR result = WERR_OK;
	uint32_t value_len = 0;
	NTSTATUS status;
	DATA_BLOB blob;

	wvalue.name = value;
	status = rpccli_winreg_QueryValue(pipe_handle,
					  mem_ctx,
					  key_handle,
					  &wvalue,
					  &type,
					  NULL,
					  (uint32_t *) &blob.length,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_query_dword: Could not query value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	if (type != REG_DWORD) {
		result = WERR_INVALID_DATATYPE;
		goto done;
	}

	if (blob.length != 4) {
		result = WERR_INVALID_DATA;
		goto done;
	}

	blob.data = (uint8_t *) TALLOC(mem_ctx, blob.length);
	if (blob.data == NULL) {
		result = WERR_NOMEM;
		goto done;
	}
	value_len = 0;

	status = rpccli_winreg_QueryValue(pipe_handle,
					  mem_ctx,
					  key_handle,
					  &wvalue,
					  &type,
					  blob.data,
					  (uint32_t *) &blob.length,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_query_dword: Could not query value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			result = ntstatus_to_werror(status);
		}
		goto done;
	}

	if (data) {
		*data = IVAL(blob.data, 0);
	}
done:
	return result;
}

/********************************************************************
 Public winreg function for spoolss
********************************************************************/

/* Set printer data over the winreg pipe. */
WERROR winreg_set_printer_dataex(TALLOC_CTX *mem_ctx,
				 struct auth_serversupplied_info *server_info,
				 const char *printer,
				 const char *key,
				 const char *value,
				 enum winreg_Type type,
				 uint8_t *data,
				 uint32_t data_size)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	char *path;
	WERROR result = WERR_OK;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	DEBUG(8, ("winreg_set_printer_dataex: Open printer key %s, value %s, access_mask: 0x%05x for [%s]\n",
			key, value, access_mask, printer));
	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					true,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_set_printer_dataex: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	wvalue.name = value;
	status = rpccli_winreg_SetValue(winreg_pipe,
					tmp_ctx,
					&key_hnd,
					wvalue,
					type,
					data,
					data_size,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_set_printer_dataex: Could not set value %s: %s\n",
			  value, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/* Get printer data over a winreg pipe. */
WERROR winreg_get_printer_dataex(TALLOC_CTX *mem_ctx,
				 struct auth_serversupplied_info *server_info,
				 const char *printer,
				 const char *key,
				 const char *value,
				 enum winreg_Type *type,
				 uint8_t **data,
				 uint32_t *data_size)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	enum winreg_Type type_in;
	char *path;
	uint8_t *data_in;
	uint32_t data_in_size = 0;
	uint32_t value_len = 0;
	WERROR result = WERR_OK;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_get_printer_dataex: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	wvalue.name = value;

	/*
	 * call QueryValue once with data == NULL to get the
	 * needed memory size to be allocated, then allocate
	 * data buffer and call again.
	 */
	status = rpccli_winreg_QueryValue(winreg_pipe,
					  tmp_ctx,
					  &key_hnd,
					  &wvalue,
					  &type_in,
					  NULL,
					  &data_in_size,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_get_printer_dataex: Could not query value %s: %s\n",
			  value, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	data_in = (uint8_t *) TALLOC(tmp_ctx, data_in_size);
	if (data_in == NULL) {
		result = WERR_NOMEM;
		goto done;
	}
	value_len = 0;

	status = rpccli_winreg_QueryValue(winreg_pipe,
					  tmp_ctx,
					  &key_hnd,
					  &wvalue,
					  &type_in,
					  data_in,
					  &data_in_size,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_get_printer_dataex: Could not query value %s: %s\n",
			  value, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			result = ntstatus_to_werror(status);
		}
		goto done;
	}

	*type = type_in;
	*data_size = data_in_size;
	if (data_in_size) {
		*data = talloc_move(mem_ctx, &data_in);
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/* Enumerate on the values of a given key and provide the data. */
WERROR winreg_enum_printer_dataex(TALLOC_CTX *mem_ctx,
				  struct auth_serversupplied_info *server_info,
				  const char *printer,
				  const char *key,
				  uint32_t *pnum_values,
				  struct spoolss_PrinterEnumValues **penum_values)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;

	struct spoolss_PrinterEnumValues *enum_values = NULL;
	uint32_t num_values = 0;
	char *path;
	WERROR result = WERR_OK;

	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_enum_printer_dataex: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	result = winreg_printer_enumvalues(tmp_ctx,
					   winreg_pipe,
					   &key_hnd,
					   &num_values,
					   &enum_values);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_enum_printer_dataex: Could not enumerate values in %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	*pnum_values = num_values;
	if (penum_values) {
		*penum_values = talloc_move(mem_ctx, &enum_values);
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/* Delete printer data over a winreg pipe. */
WERROR winreg_delete_printer_dataex(TALLOC_CTX *mem_ctx,
				    struct auth_serversupplied_info *server_info,
				    const char *printer,
				    const char *key,
				    const char *value)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	char *path;
	WERROR result = WERR_OK;
	NTSTATUS status;

	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_delete_printer_dataex: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	wvalue.name = value;
	status = rpccli_winreg_DeleteValue(winreg_pipe,
					   tmp_ctx,
					   &key_hnd,
					   wvalue,
					   &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_delete_printer_dataex: Could not delete value %s: %s\n",
			  value, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/* Enumerate on the subkeys of a given key and provide the data. */
WERROR winreg_enum_printer_key(TALLOC_CTX *mem_ctx,
			       struct auth_serversupplied_info *server_info,
			       const char *printer,
			       const char *key,
			       uint32_t *pnum_subkeys,
			       const char ***psubkeys)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	char *path;
	const char **subkeys = NULL;
	uint32_t num_subkeys = -1;

	WERROR result = WERR_OK;

	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_enum_printer_key: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	result = winreg_printer_enumkeys(tmp_ctx,
					 winreg_pipe,
					 &key_hnd,
					 &num_subkeys,
					 &subkeys);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_enum_printer_key: Could not enumerate subkeys in %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	*pnum_subkeys = num_subkeys;
	if (psubkeys) {
		*psubkeys = talloc_move(mem_ctx, &subkeys);
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/* Delete a key with subkeys of a given printer. */
WERROR winreg_delete_printer_key(TALLOC_CTX *mem_ctx,
				 struct auth_serversupplied_info *server_info,
				 const char *printer,
				 const char *key)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	char *keyname;
	char *path;
	WERROR result;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					key,
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		/* key doesn't exist */
		if (W_ERROR_EQUAL(result, WERR_BADFILE)) {
			result = WERR_OK;
			goto done;
		}

		DEBUG(0, ("winreg_delete_printer_key: Could not open key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

	if (is_valid_policy_hnd(&key_hnd)) {
		rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
	}

	keyname = talloc_asprintf(tmp_ctx,
				  "%s\\%s",
				  path,
				  key);
	if (keyname == NULL) {
		result = WERR_NOMEM;
		goto done;
	}

	result = winreg_printer_delete_subkeys(tmp_ctx,
					       winreg_pipe,
					       &hive_hnd,
					       access_mask,
					       keyname);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_delete_printer_key: Could not delete key %s: %s\n",
			  key, win_errstr(result)));
		goto done;
	}

done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_update_changeid(TALLOC_CTX *mem_ctx,
				      struct auth_serversupplied_info *server_info,
				      const char *printer)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	char *path;
	WERROR result;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					"",
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_update_changeid: Could not open key %s: %s\n",
			  path, win_errstr(result)));
		goto done;
	}

	result = winreg_printer_write_dword(tmp_ctx,
					    winreg_pipe,
					    &key_hnd,
					    "ChangeID",
					    winreg_printer_rev_changeid());
	if (!W_ERROR_IS_OK(result)) {
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_get_changeid(TALLOC_CTX *mem_ctx,
				   struct auth_serversupplied_info *server_info,
				   const char *printer,
				   uint32_t *pchangeid)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	uint32_t changeid = 0;
	char *path;
	WERROR result;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	path = winreg_printer_data_keyname(tmp_ctx, printer);
	if (path == NULL) {
		TALLOC_FREE(tmp_ctx);
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					path,
					"",
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_get_changeid: Could not open key %s: %s\n",
			  path, win_errstr(result)));
		goto done;
	}

	DEBUG(0, ("winreg_printer_get_changeid: get changeid from %s\n", path));
	result = winreg_printer_query_dword(tmp_ctx,
					    winreg_pipe,
					    &key_hnd,
					    "ChangeID",
					    &changeid);
	if (!W_ERROR_IS_OK(result)) {
		goto done;
	}

	if (pchangeid) {
		*pchangeid = changeid;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

/*
 * The special behaviour of the spoolss forms is documented at the website:
 *
 * Managing Win32 Printserver Forms
 * http://unixwiz.net/techtips/winspooler-forms.html
 */

WERROR winreg_printer_addform1(TALLOC_CTX *mem_ctx,
			       struct auth_serversupplied_info *server_info,
			       struct spoolss_AddFormInfo1 *form)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	DATA_BLOB blob;
	uint32_t num_info = 0;
	union spoolss_FormInfo *info = NULL;
	uint32_t i;
	WERROR result;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					TOP_LEVEL_CONTROL_FORMS_KEY,
					"",
					true,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_addform1: Could not open key %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	result = winreg_printer_enumforms1(tmp_ctx, server_info, &num_info, &info);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_addform: Could not enum keys %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	/* If form name already exists or is builtin return ALREADY_EXISTS */
	for (i = 0; i < num_info; i++) {
		if (strequal(info[i].info1.form_name, form->form_name)) {
			result = WERR_FILE_EXISTS;
			goto done;
		}
	}

	wvalue.name = form->form_name;

	blob = data_blob_talloc(tmp_ctx, NULL, 32);
	SIVAL(blob.data,  0, form->size.width);
	SIVAL(blob.data,  4, form->size.height);
	SIVAL(blob.data,  8, form->area.left);
	SIVAL(blob.data, 12, form->area.top);
	SIVAL(blob.data, 16, form->area.right);
	SIVAL(blob.data, 20, form->area.bottom);
	SIVAL(blob.data, 24, num_info + 1); /* FIXME */
	SIVAL(blob.data, 28, form->flags);

	status = rpccli_winreg_SetValue(winreg_pipe,
					tmp_ctx,
					&key_hnd,
					wvalue,
					REG_BINARY,
					blob.data,
					blob.length,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_addform1: Could not set value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(info);
	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_enumforms1(TALLOC_CTX *mem_ctx,
				 struct auth_serversupplied_info *server_info,
				 uint32_t *pnum_info,
				 union spoolss_FormInfo **pinfo)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	union spoolss_FormInfo *info;
	struct spoolss_PrinterEnumValues *enum_values = NULL;
	uint32_t num_values = 0;
	uint32_t num_builtin = ARRAY_SIZE(builtin_forms1);
	uint32_t i;
	WERROR result;
	TALLOC_CTX *tmp_ctx;

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					TOP_LEVEL_CONTROL_FORMS_KEY,
					"",
					true,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		/* key doesn't exist */
		if (W_ERROR_EQUAL(result, WERR_BADFILE)) {
			result = WERR_OK;
			goto done;
		}

		DEBUG(0, ("winreg_printer_enumforms1: Could not open key %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	result = winreg_printer_enumvalues(tmp_ctx,
					   winreg_pipe,
					   &key_hnd,
					   &num_values,
					   &enum_values);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_enumforms1: Could not enumerate values in %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	info = TALLOC_ARRAY(tmp_ctx, union spoolss_FormInfo, num_builtin + num_values);
	if (info == NULL) {
		result = WERR_NOMEM;
		goto done;
	}

	/* Enumerate BUILTIN forms */
	for (i = 0; i < num_builtin; i++) {
		info[i].info1 = builtin_forms1[i];
	}

	/* Enumerate registry forms */
	for (i = 0; i < num_values; i++) {
		union spoolss_FormInfo val;

		if (enum_values[i].type != REG_BINARY ||
		    enum_values[i].data_length != 32) {
			continue;
		}

		val.info1.form_name = talloc_strdup(info, enum_values[i].value_name);
		if (val.info1.form_name == NULL) {
			result = WERR_NOMEM;
			goto done;
		}

		val.info1.size.width  = IVAL(enum_values[i].data->data,  0);
		val.info1.size.height = IVAL(enum_values[i].data->data,  4);
		val.info1.area.left   = IVAL(enum_values[i].data->data,  8);
		val.info1.area.top    = IVAL(enum_values[i].data->data, 12);
		val.info1.area.right  = IVAL(enum_values[i].data->data, 16);
		val.info1.area.bottom = IVAL(enum_values[i].data->data, 20);
		/* skip form index      IVAL(enum_values[i].data->data, 24)));*/
		val.info1.flags       = IVAL(enum_values[i].data->data, 28);

		info[i + num_builtin] = val;
	}

	*pnum_info = num_builtin + num_values;
	if (pinfo) {
		*pinfo = talloc_move(mem_ctx, &info);
	}

done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(enum_values);
	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_deleteform1(TALLOC_CTX *mem_ctx,
				  struct auth_serversupplied_info *server_info,
				  const char *form_name)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	uint32_t num_builtin = ARRAY_SIZE(builtin_forms1);
	uint32_t i;
	WERROR result = WERR_OK;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx;

	for (i = 0; i < num_builtin; i++) {
		if (strequal(builtin_forms1[i].form_name, form_name)) {
			return WERR_INVALID_PARAMETER;
		}
	}

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					TOP_LEVEL_CONTROL_FORMS_KEY,
					"",
					false,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_deleteform1: Could not open key %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		if (W_ERROR_EQUAL(result, WERR_BADFILE)) {
			result = WERR_INVALID_FORM_NAME;
		}
		goto done;
	}

	wvalue.name = form_name;
	status = rpccli_winreg_DeleteValue(winreg_pipe,
					   tmp_ctx,
					   &key_hnd,
					   wvalue,
					   &result);
	if (!NT_STATUS_IS_OK(status)) {
		/* If the value doesn't exist, return WERR_INVALID_FORM_NAME */
		if (W_ERROR_EQUAL(result, WERR_BADFILE)) {
			result = WERR_INVALID_FORM_NAME;
			goto done;
		}
		DEBUG(0, ("winreg_printer_delteform1: Could not delete value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_setform1(TALLOC_CTX *mem_ctx,
			       struct auth_serversupplied_info *server_info,
			       const char *form_name,
			       struct spoolss_AddFormInfo1 *form)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	DATA_BLOB blob;
	uint32_t num_builtin = ARRAY_SIZE(builtin_forms1);
	uint32_t i;
	WERROR result;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx = NULL;

	for (i = 0; i < num_builtin; i++) {
		if (strequal(builtin_forms1[i].form_name, form->form_name)) {
			result = WERR_INVALID_PARAM;
			goto done;
		}
	}

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					TOP_LEVEL_CONTROL_FORMS_KEY,
					"",
					true,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_setform1: Could not open key %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	/* If form_name != form->form_name then we renamed the form */
	if (strequal(form_name, form->form_name)) {
		result = winreg_printer_deleteform1(tmp_ctx, server_info, form_name);
		if (!W_ERROR_IS_OK(result)) {
			DEBUG(0, ("winreg_printer_setform1: Could not open key %s: %s\n",
				  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
			goto done;
		}
	}

	wvalue.name = form->form_name;

	blob = data_blob_talloc(tmp_ctx, NULL, 32);
	SIVAL(blob.data,  0, form->size.width);
	SIVAL(blob.data,  4, form->size.height);
	SIVAL(blob.data,  8, form->area.left);
	SIVAL(blob.data, 12, form->area.top);
	SIVAL(blob.data, 16, form->area.right);
	SIVAL(blob.data, 20, form->area.bottom);
	SIVAL(blob.data, 24, 42);
	SIVAL(blob.data, 28, form->flags);

	status = rpccli_winreg_SetValue(winreg_pipe,
					tmp_ctx,
					&key_hnd,
					wvalue,
					REG_BINARY,
					blob.data,
					blob.length,
					&result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_setform1: Could not set value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}

WERROR winreg_printer_getform1(TALLOC_CTX *mem_ctx,
			       struct auth_serversupplied_info *server_info,
			       const char *form_name,
			       struct spoolss_FormInfo1 *r)
{
	uint32_t access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	struct rpc_pipe_client *winreg_pipe = NULL;
	struct policy_handle hive_hnd, key_hnd;
	struct winreg_String wvalue;
	enum winreg_Type type_in;
	uint8_t *data_in;
	uint32_t data_in_size = 0;
	uint32_t value_len = 0;
	uint32_t num_builtin = ARRAY_SIZE(builtin_forms1);
	uint32_t i;
	WERROR result;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx;

	/* check builtin forms first */
	for (i = 0; i < num_builtin; i++) {
		if (strequal(builtin_forms1[i].form_name, form_name)) {
			*r = builtin_forms1[i];
			return WERR_OK;
		}
	}

	tmp_ctx = talloc_new(mem_ctx);
	if (tmp_ctx == NULL) {
		return WERR_NOMEM;
	}

	ZERO_STRUCT(hive_hnd);
	ZERO_STRUCT(key_hnd);

	result = winreg_printer_openkey(tmp_ctx,
					server_info,
					&winreg_pipe,
					TOP_LEVEL_CONTROL_FORMS_KEY,
					"",
					true,
					access_mask,
					&hive_hnd,
					&key_hnd);
	if (!W_ERROR_IS_OK(result)) {
		DEBUG(0, ("winreg_printer_getform1: Could not open key %s: %s\n",
			  TOP_LEVEL_CONTROL_FORMS_KEY, win_errstr(result)));
		goto done;
	}

	wvalue.name = form_name;

	/*
	 * call QueryValue once with data == NULL to get the
	 * needed memory size to be allocated, then allocate
	 * data buffer and call again.
	 */
	status = rpccli_winreg_QueryValue(winreg_pipe,
					  tmp_ctx,
					  &key_hnd,
					  &wvalue,
					  &type_in,
					  NULL,
					  &data_in_size,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_getform1: Could not query value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	data_in = (uint8_t *) TALLOC(tmp_ctx, data_in_size);
	if (data_in == NULL) {
		result = WERR_NOMEM;
		goto done;
	}
	value_len = 0;

	status = rpccli_winreg_QueryValue(winreg_pipe,
					  tmp_ctx,
					  &key_hnd,
					  &wvalue,
					  &type_in,
					  data_in,
					  &data_in_size,
					  &value_len,
					  &result);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("winreg_printer_getform1: Could not query value %s: %s\n",
			  wvalue.name, nt_errstr(status)));
		if (!W_ERROR_IS_OK(result)) {
			goto done;
		}
		result = ntstatus_to_werror(status);
		goto done;
	}

	r->form_name = talloc_strdup(mem_ctx, form_name);
	if (r->form_name == NULL) {
		result = WERR_NOMEM;
		goto done;
	}

	r->size.width  = IVAL(data_in,  0);
	r->size.height = IVAL(data_in,  4);
	r->area.left   = IVAL(data_in,  8);
	r->area.top    = IVAL(data_in, 12);
	r->area.right  = IVAL(data_in, 16);
	r->area.bottom = IVAL(data_in, 20);
	/* skip index    IVAL(data_in, 24)));*/
	r->flags       = IVAL(data_in, 28);

	result = WERR_OK;
done:
	if (winreg_pipe != NULL) {
		if (is_valid_policy_hnd(&key_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &key_hnd, NULL);
		}
		if (is_valid_policy_hnd(&hive_hnd)) {
			rpccli_winreg_CloseKey(winreg_pipe, tmp_ctx, &hive_hnd, NULL);
		}
	}

	TALLOC_FREE(tmp_ctx);
	return result;
}
