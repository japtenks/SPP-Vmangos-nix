DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20260414190000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20260414190000');
-- Add your query below.


IF EXISTS (
  SELECT 1
  FROM information_schema.tables
  WHERE table_schema = DATABASE()
    AND table_name = 'ai_playerbot_db_store'
) THEN
  ALTER TABLE `ai_playerbot_db_store`
    MODIFY COLUMN `key` varchar(128) NOT NULL;
END IF;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
