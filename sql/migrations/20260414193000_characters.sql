CREATE TABLE IF NOT EXISTS `ai_playerbot_social_memory` (
  `owner_guid` bigint unsigned NOT NULL,
  `target_guid` bigint unsigned NOT NULL,
  `affinity` float NOT NULL DEFAULT '0',
  `hostility` float NOT NULL DEFAULT '0',
  `last_interaction` int unsigned NOT NULL DEFAULT '0',
  `flags` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`owner_guid`,`target_guid`),
  KEY `idx_target_guid` (`target_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ai_playerbot_guild_hubs` (
  `guild_id` int unsigned NOT NULL,
  `preferred_area_id` int unsigned NOT NULL DEFAULT '0',
  `contenders` text NOT NULL,
  `last_update` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
