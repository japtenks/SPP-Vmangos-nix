CREATE TABLE IF NOT EXISTS `ahbot_market_state` (
  `market_id` bigint(20) unsigned NOT NULL,
  `treasury` bigint(20) NOT NULL DEFAULT '0',
  `updated_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`market_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `ahbot_auction` (
  `auction_id` bigint(20) unsigned NOT NULL,
  `market_id` bigint(20) unsigned NOT NULL DEFAULT '0',
  `seller_guid` bigint(20) unsigned NOT NULL DEFAULT '0',
  `retention_pct` bigint(20) unsigned NOT NULL DEFAULT '30',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  `created_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`auction_id`),
  KEY `idx_ahbot_auction_market` (`market_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
