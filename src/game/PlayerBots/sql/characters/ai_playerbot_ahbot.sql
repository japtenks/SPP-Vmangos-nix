DROP TABLE IF EXISTS `ahbot_history`;
DROP TABLE IF EXISTS `ahbot_category`;
DROP TABLE IF EXISTS `ahbot_price`;
DROP TABLE IF EXISTS `ahbot_market_state`;
DROP TABLE IF EXISTS `ahbot_auction`;

CREATE TABLE `ahbot_history` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `buytime` bigint(20) NOT NULL,
  `item` bigint(20) NOT NULL,
  `bid` bigint(20) NOT NULL,
  `buyout` bigint(20) NOT NULL,
  `won` bigint(20) NOT NULL,
  `category` varchar(45) DEFAULT NULL,
  `auction_house` bigint(20) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `won` (`won`),
  KEY `category` (`category`),
  KEY `auction_house` (`auction_house`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `ahbot_category` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `category` varchar(45) DEFAULT NULL,
  `multiplier` decimal(20, 2) NOT NULL,
  `max_auction_count` bigint(20) NOT NULL,
  `expire_time` bigint(20) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `helper` (`category`, `multiplier`, `expire_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `ahbot_price` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `item` varchar(45) DEFAULT NULL,
  `price` decimal(20, 2) NOT NULL,
  `auction_house` bigint(20) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `ahbot_price_item` (`item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `ahbot_market_state` (
  `market_id` bigint(20) unsigned NOT NULL,
  `treasury` bigint(20) NOT NULL DEFAULT '0',
  `updated_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`market_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `ahbot_auction` (
  `auction_id` bigint(20) unsigned NOT NULL,
  `market_id` bigint(20) unsigned NOT NULL DEFAULT '0',
  `seller_guid` bigint(20) unsigned NOT NULL DEFAULT '0',
  `retention_pct` bigint(20) unsigned NOT NULL DEFAULT '30',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  `created_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`auction_id`),
  KEY `idx_ahbot_auction_market` (`market_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

create index idx_ahbot_history_item on ahbot_history(item);
create index idx_ahbot_history_buytime on ahbot_history(buytime);
create index idx_ahbot_price_auction_house on ahbot_price(auction_house);
