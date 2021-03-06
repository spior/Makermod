
#include "q_shared.h"
#include "g_local.h" // for gclient_t
#include "../../libs/cjson/cJSON.h" // for JSON functions
#include "makermod.h" // for jail_t

jail_t jails;

jail_t *jails_add(vec3_t origin, vec3_t angles)
{
	jail_t *root = &jails;
	while (root->next)
	{
		if (VectorCompare(origin, root->origin) &&
			VectorCompare(angles, root->angles))
			return NULL;
		root = root->next;
	}

	jail_t *newItem = calloc(1, sizeof(jail_t));
	//	newItem->prev = root;
	root->next = newItem;
	newItem->prev = root;
	VectorCopy(origin, newItem->origin);
	VectorCopy(angles, newItem->angles);
	jails.count++;

	return newItem;
}

jail_t *MM_GetJail(void)
{
	jail_t *root = jails.next;

	if (jails.count == 0)
		return NULL;

	int randJail = Q_irand(0, jails.count-1);

	int i = 0;
	while (root)
	{
		if (i == randJail)
			return root;
		i++;
		root = root->next;
	}

	return NULL;
}

void MM_ReadJails(const char *fileData)
{
	cJSON * root = cJSON_Parse(fileData);
	int len = root ? cJSON_GetArraySize(root) : 0;

	for (int i = 0; i < len; i++)
	{
		cJSON *item = cJSON_GetArrayItem(root, i);
		if (!item)
		{
			G_LogPrintf(__FUNCTION__"(): item %i is NULL, skipping.\n", i);
			continue;
		}

		cJSON *origin = cJSON_GetObjectItem(item, "origin");
		if (!origin)
		{
			G_LogPrintf(__FUNCTION__"(): item %i has NULL origin, skipping.\n", i);
			continue;
		}

		if (cJSON_GetArraySize(origin) != 3)
		{
			G_LogPrintf(__FUNCTION__"(): item %i has corrupted origin, skipping.\n", i);
			continue;
		}

		float origin_v[3] = { 0 };
		qboolean _continue = qfalse;
		for (int j = 0; j < 3; j++)
		{
			cJSON *origin_m = cJSON_GetArrayItem(origin, j);
			if (origin_m->type == cJSON_Number)
				origin_v[j] = cJSON_GetArrayItem(origin, j)->valuedouble;
			else
			{
				G_LogPrintf(__FUNCTION__"(): item %i has corrupted origin axis, skipping.\n", i);
				_continue = qtrue;
			}
		}
		if (_continue) continue;

		cJSON *angles = cJSON_GetObjectItem(item, "angles");
		if (!angles)
		{
			G_LogPrintf(__FUNCTION__"(): item %i has NULL angles, skipping.\n", i);
			continue;
		}

		if (cJSON_GetArraySize(angles) != 3)
		{
			G_LogPrintf(__FUNCTION__"(): item %i has corrupted angles, skipping.\n", i);
			continue;
		}

		float angles_v[3] = { 0 };
		for (int j = 0; j < 3; j++)
		{
			cJSON *angles_m = cJSON_GetArrayItem(angles, j);
			if (angles_m->type == cJSON_Number)
				angles_v[j] = cJSON_GetArrayItem(angles, j)->valuedouble;
			else
			{
				G_LogPrintf(__FUNCTION__"(): item %i has corrupted angles axis, skipping.\n", i);
				_continue = qtrue;
			}
		}
		if (_continue) continue;

		jails_add(origin_v, angles_v);
	}

	cJSON_Delete(root);
}

void MM_WriteJails(char *fileData, int *fileSize)
{
	cJSON *json_arr = cJSON_CreateArray();
	jail_t *root = jails.next;
	while (root)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON *origin = cJSON_CreateArray();
		cJSON *angles = cJSON_CreateArray();

		if (!item || !angles)
		{
			G_LogPrintf(__FUNCTION__"(): failed to create JSON object/array.\n");
			continue;
		}

		for (int i = 0; i < 3; i++)
			cJSON_AddItemToArray(origin, cJSON_CreateNumber(root->origin[i]));

		for (int i = 0; i < 3; i++)
			cJSON_AddItemToArray(angles, cJSON_CreateNumber(root->angles[i]));

		cJSON_AddItemToObject(item, "origin", origin);
		cJSON_AddItemToObject(item, "angles", angles);

		cJSON_AddItemToArray(json_arr, item);

		char *s = cJSON_Print(json_arr);

		while (strlen(s) >= MAX_DATA_SIZE)
		{
			G_LogPrintf(__FUNCTION__"(): maximum filesize reached, removing items from start.\n");
			cJSON_DeleteItemFromArray(json_arr, 0);
		}

		root = root->next;
	}

	Q_strncpyz(fileData, cJSON_Print(json_arr), *fileSize);
	*fileSize = strlen(fileData);
	cJSON_Delete(json_arr);
}

void MM_JailClient(gentity_t *ent, qboolean respawn)
{
	jail_t *jail = MM_GetJail();

	if (jail == NULL)
	{
		MM_SendMessage(ent - g_entities, "print \"No jail spots found on this map. Please add a jail spot before trying to jail a client.\n\"");
		ent->client->sess.jailed = qfalse;
	}
	else
	{
		ent->client->sess.jailed = qtrue;

		if(respawn)
			TeleportPlayer(ent, jail->origin, jail->angles);
		else
		{
			G_SetOrigin(ent, jail->origin);
			VectorCopy(jail->origin, ent->client->ps.origin);
			SetClientViewAngle(ent, jail->angles);
		}

		G_Unempower(ent);
		ent->client->ps.fd.forcePowersKnown = 0;
		ent->client->ps.stats[STAT_WEAPONS] = (1 << WP_MELEE);
		ent->client->ps.weapon = WP_MELEE;
		ent->client->ps.pm_flags &= ~PM_NOCLIP;
	}
}

qboolean HasPermission(gentity_t* ent, int permissionClass);
int ClientNumberFromString(gentity_t *to, char *s);
void Cmd_Jail_f(gentity_t *ent)
{
	char buffer[MAX_TOKEN_CHARS];
	int clientNum;

	if (!HasPermission(ent, PERMISSION_JAIL))
		return;

	if (trap_Argc() != 2)
	{
		MM_SendMessage(ent - g_entities, va("print \"Command usage: mjail <client-name-or-number>\n\""));
		return;
	}

	trap_Argv(1, buffer, sizeof(buffer));

	clientNum = ClientNumberFromString(ent, buffer);

	if (clientNum == -1)
	{
		MM_SendMessage(ent - g_entities, va("print \"ERROR: Could not identify player %s\n\"", buffer));
		return;
	}

	gentity_t *target = &g_entities[clientNum];

	if (target->client->sess.jailed == qfalse)
		MM_JailClient(target, qfalse);
	else
	{
		target->client->sess.jailed = qfalse;
		ClientSpawn(target); // not sure how safe this is but what the heck lmao
	}
	
}

void Cmd_NewJail_f(gentity_t *ent)
{
	if (!HasPermission(ent, PERMISSION_JAIL))
		return;

	if (trap_Argc() != 1)
	{
		MM_SendMessage(ent - g_entities, va("print \"Command usage: mnewjail\n\""));
		return;
	}

	if(jails_add(ent->client->ps.origin, ent->client->ps.viewangles))
		MM_SendMessage(ent - g_entities, va("print \"New jail spot created.\n\""));
	else
		MM_SendMessage(ent - g_entities, va("print \"Error creating new jail spot.\n\""));

	vmCvar_t mapname;
	trap_Cvar_Register(&mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM);
	MM_WriteData(va("jails\\%s.json", mapname.string), MM_WriteJails);
}

void Cmd_DelJail_f(gentity_t *ent)
{
	char buffer[MAX_TOKEN_CHARS];
	if (!HasPermission(ent, PERMISSION_JAIL))
		return;

	if (trap_Argc() != 2)
	{
		MM_SendMessage(ent - g_entities, va("print \"Command usage: mdeljail <id>\n\""));
		return;
	}

	trap_Argv(1, buffer, sizeof(buffer));

	int id = atoi(buffer);
	jail_t *root = jails.next;

	for (int i = 0; i < id; i++)
		if (root == NULL) break;
		else
			root = root->next;

	if (root)
	{
		if(root->prev) root->prev->next = root->next;
		if(root->next) root->next->prev = root->prev;
		free(root);
		jails.count--;
		MM_SendMessage(ent - g_entities, va("print \"Jail spot removed.\n\""));
	}
	else
		MM_SendMessage(ent - g_entities, va("print \"Error removing jail spot.\n\""));

	vmCvar_t mapname;
	trap_Cvar_Register(&mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM);
	MM_WriteData(va("jails\\%s.json", mapname.string), MM_WriteJails);
}

void Cmd_ListJail_f(gentity_t *ent)
{
	if (!HasPermission(ent, PERMISSION_JAIL))
		return;

	if (trap_Argc() != 1)
	{
		MM_SendMessage(ent - g_entities, va("print \"Command usage: mlistjail\n\""));
		return;
	}

	MM_SendMessage(ent - g_entities, "print \"Jail spots:\n\"");
	MM_SendMessage(ent - g_entities, "print \"  Origin\n\"");

	jail_t *root = jails.next;
	int i = 0;
	while(root)
	{
		MM_SendMessage(ent - g_entities, va("print \"%i (%i %i %i)\n\"", i++, (int)root->origin[0], (int)root->origin[1], (int)root->origin[2]));
		root = root->next;
	}
}