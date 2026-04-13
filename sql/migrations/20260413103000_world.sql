DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20260413103000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20260413103000');
-- Add your query below.

CREATE TABLE IF NOT EXISTS `ai_playerbot_premade_spec` (
  `class_id` tinyint unsigned NOT NULL,
  `spec_id` smallint unsigned NOT NULL,
  `level` tinyint unsigned NOT NULL,
  `name` varchar(120) NOT NULL,
  `probability` smallint unsigned NOT NULL DEFAULT 100,
  `talent_link` varchar(255) NOT NULL,
  `core_build` smallint unsigned NOT NULL,
  PRIMARY KEY (`core_build`, `class_id`, `spec_id`, `level`),
  KEY `idx_ai_playerbot_premade_spec_lookup` (`class_id`, `spec_id`, `level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `ai_playerbot_premade_spec` WHERE `core_build` = 5875;

INSERT INTO `ai_playerbot_premade_spec`
(`class_id`, `spec_id`, `level`, `name`, `probability`, `talent_link`, `core_build`)
VALUES
(1, 0, 60, 'pve arms', 100, '303050213520105001-0505300502', 5875),
(1, 1, 60, 'pve fury', 100, '30305001302-05050005525010051', 5875),
(1, 2, 60, 'pve prot', 100, '05005001-05-55250110530101051', 5875),
(1, 3, 60, 'pvp arms', 100, '023050213320105031-55000131005', 5875),
(1, 4, 60, 'pvp fury', 100, '30325001302-05052130025012051', 5875),
(1, 5, 60, 'pvp prot', 100, '05003--55255113032021251', 5875),
(1, 6, 60, 'fury slam', 100, '30305001322-05050005005510051', 5875),
(1, 7, 60, 'arms axes', 100, '303050213525100001-05050005005', 5875),
(1, 8, 60, 'arms maces', 100, '303050213520150001-05050005005', 5875),
(1, 9, 60, 'arms swords', 100, '303050213520105001-05050005005', 5875),
(1, 10, 60, 'arms polearms', 100, '303050213520100501-05050005005', 5875),
(1, 11, 60, 'furyprot', 100, '-05050005505010051-502501105', 5875),
(1, 12, 60, 'furyprot (slam)', 100, '-05500005005510051-502501105', 5875),
(1, 13, 60, 'furyprot (demo shout)', 100, '-05500005505010051-502501105', 5875),
(1, 14, 60, '2h fury', 100, '3500502103-05050005505010051', 5875),
(1, 15, 60, 'fury no deep wounds', 100, '05005001-05053125525010051', 5875),
(2, 0, 60, 'pve dps ret (basic ret)', 100, '550001-503-542300512003151', 5875),
(2, 1, 60, 'pve dps ret (geared ret)', 100, '5550012--552300512003151', 5875),
(2, 2, 60, 'pve heal holy (sanctuary)', 100, '0550312152105-503251000131', 5875),
(2, 3, 60, 'pve heal holy (prot! holy shock taunt)', 100, '55153122501001-053200334', 5875),
(2, 4, 60, 'pvp heal holy', 80, '05503120521351-05025033', 5875),
(2, 5, 60, 'pvp heal Holy', 80, '05503112521051-05324100032', 5875),
(2, 6, 60, 'pvp tank prot', 80, '500501-053050335001051-05004', 5875),
(2, 7, 60, 'pvp dps ret', 80, '505001-0531-502310512203151', 5875),
(2, 8, 60, 'pvp dps ret (Loaded Reck Bomb)', 80, '-0532010153005-50203151220311', 5875),
(3, 0, 60, 'pve dps mm (mm/sv)', 60, '2-05251030513051-33202004103', 5875),
(3, 1, 60, 'pve dps mm (mm/bm)', 60, '55000000505-05251030513051', 5875),
(3, 2, 60, 'pve dps mm (mm/sv)', 60, '5-05351030503051-33050022', 5875),
(3, 3, 60, 'pve dps bm (farmer)', 60, '5000322050521231-054510005', 5875),
(3, 4, 60, 'pve dps mm', 60, '53000200505-05351030503051', 5875),
(3, 5, 60, 'pvp dps surv', 100, '-5105103051-330005241030315', 5875),
(3, 6, 60, 'pvp dps mm', 100, '-52051030513051-03222304103', 5875),
(3, 7, 60, 'pvp dps bm', 100, '1500322150501051-051510305', 5875),
(4, 0, 30, 'pve dps assasination', 100, '005302105041', 5875),
(4, 0, 52, 'pve dps assasination', 100, '00532310505105-3203-05', 5875),
(4, 0, 60, 'pve dps assasination', 100, '00532310505105-320305002001-05', 5875),
(4, 1, 20, 'pve dps combat', 100, '-02305001', 5875),
(4, 1, 30, 'pve dps combat', 100, '-02305501000401', 5875),
(4, 1, 56, 'pve dps combat', 100, '005302105-0230550100040140231', 5875),
(4, 1, 60, 'pve dps combat', 100, '00530310503-0230550100050130231', 5875),
(4, 2, 60, 'pve dps combat (swords)', 100, '005323105-3210052020050150231', 5875),
(4, 3, 60, 'pve dps combat (daggers)', 100, '005023005-3203052020550100201-05', 5875),
(4, 4, 60, 'pve dps combat (daggers2)', 100, '305020005-02505501005001-50005002', 5875),
(4, 5, 60, 'pve dps combat', 100, '305023005-3200550100050150231', 5875),
(4, 6, 60, 'pvp dps combat (swords)', 100, '005323105-3210052020050150231', 5875),
(4, 7, 60, 'pvp dps combat (daggers)', 100, '005023005-3203052020550100201-05', 5875),
(4, 8, 60, 'pvp dps combat (daggers2)', 100, '305020005-02505501005001-50005002', 5875),
(4, 9, 60, 'pvp dps combat', 100, '305023005-3200550100050150231', 5875),
(5, 0, 60, 'pve heal disc', 100, '505230130505101-0250510313', 5875),
(5, 1, 60, 'pve heal holy', 100, '5012301305001-025051031300055', 5875),
(5, 2, 60, 'pve dps shadow', 100, '50520013--5032504103501051', 5875),
(5, 3, 60, 'pvp dps disc', 100, '500232130225151-20525100202', 5875),
(5, 4, 60, 'pvp heal holy', 100, '5002321331-2250511323001051', 5875),
(5, 5, 60, 'pvp dps shadow', 100, '50023212032--0502322103511051', 5875),
(7, 0, 60, 'pve dps elem (elemental mastery)', 100, '550331050002151--05204301005', 5875),
(7, 1, 60, 'pve dps elem (nature''s swiftness)', 100, '55030105030215--0520430100501', 5875),
(7, 2, 60, 'pve dps elem (force of nature + hand of edward the odd)', 100, '5500315023101--0500535100501', 5875),
(7, 3, 60, 'pve heal resto (pure)', 100, '-5-550350512553151', 5875),
(7, 4, 60, 'pve heal resto (melee support resto)', 100, '-5120202-550350510503151', 5875),
(7, 5, 60, 'pvp dps elem (nature''s swiftness)', 100, '55010105230215--0500531100501', 5875),
(7, 6, 60, 'pvp dps elem (elemental mastery)', 100, '550101052302151--05005311005', 5875),
(7, 7, 60, 'pvp dps elem (elemental mastery)', 100, '55000135030215--0523031100501', 5875),
(7, 8, 60, 'pvp dps enhan (2hand)', 100, '0523015003-5025230104003151', 5875),
(7, 9, 60, 'pvp heal resto', 100, '-5-550350512553151', 5875),
(8, 0, 60, 'pve dps arcane', 33, '2300450310031531--053500030013', 5875),
(8, 1, 60, 'pve dps fire', 33, '230055000002-5052000123033151-003', 5875),
(8, 2, 60, 'pve dps frost (winter''s chill spec)', 33, '230045030003--05350003101301351', 5875),
(8, 3, 60, 'pve dps frost (frost build for farming)', 33, '-055002302003-24250230102051301', 5875),
(8, 4, 60, 'pve dps frost (frost-arcane)', 33, '250005030011--05050203132351301', 5875),
(8, 5, 60, 'pve dps frost (fun)', 33, '230005--05353203101351351', 5875),
(8, 6, 60, 'pve dps frost (aoe farm)', 33, '230055001003--25300233132301301', 5875),
(8, 7, 60, 'pve dps arcane (glass cannon)', 33, '2300550310031531--053500030003', 5875),
(8, 8, 60, 'pve dps frost', 33, '230045200003--05050033132301051', 5875),
(8, 9, 60, 'pvp dps frost', 100, '20500322102--05053203102351301', 5875),
(8, 10, 60, 'pvp dps frost (frosted fun)', 100, '230005001--05350233132351301', 5875),
(8, 11, 60, 'pvp dps arcane (venruki)', 100, '2300512312201531-05523201-002', 5875),
(9, 0, 60, 'pve dps demo (ds/ruin)', 100, '25002-205030015221-52500051020001', 5875),
(9, 1, 60, 'pve dps demo (succubus sacrifice)', 100, '25002-2050300142301-52500051020001', 5875),
(9, 2, 60, 'pve dps dest (imp lord)', 100, '05002-2350300142001-52500051220001', 5875),
(9, 3, 60, 'pve dps demo (sm/ruin)', 100, '5502203112201105--50502051020001', 5875),
(9, 4, 60, 'pve dps affli', 100, '55002530122010051-2351050102', 5875),
(9, 5, 60, 'pvp dps demo (sl)', 71, '05002-20503011525010512-05500001', 5875),
(9, 6, 60, 'pvp dps demo (soul link/ shadowburn)', 71, '35-2050310152501051-50500011', 5875),
(9, 7, 60, 'pvp dps demo (soul link/ nightfall)', 71, '35000232122-2050310152501051', 5875),
(9, 8, 60, 'pvp dps affli (sm/ruin)', 71, '2500213212201135--50500051022001', 5875),
(9, 9, 60, 'pvp dps affli (drakedog)', 71, '05002-205-0555005102205151', 5875),
(9, 10, 60, 'pvp dps affli (sm/ruin)', 71, '3500023212201135--50520051020001', 5875),
(9, 11, 60, 'pvp dps destro (conflagrate)', 71, '35000231122-001-5052005102005141', 5875),
(11, 0, 60, 'pve dps feral', 100, '014005301-5500021323202151-05', 5875),
(11, 1, 60, 'pve dps feral (dps/tank hybrid)', 100, '014005001-5050301323222151-05', 5875),
(11, 2, 60, 'pve dps resto (swiftmend spec)', 100, '0143002002--505503105315051', 5875),
(11, 3, 60, 'pve dps resto (regrowth spec bear aoe farm)', 100, '4100053312011--50530310031405', 5875),
(11, 4, 60, 'pve dps resto (resto-balance)', 100, '01430433020013--505103105115', 5875),
(11, 5, 60, 'pvp dps resto (swiftmend / feral charge)', 100, '0140002-5002321-055103105315011', 5875),
(11, 6, 60, 'pvp dps resto', 100, '0140003-5000501-055103105315021', 5875),
(11, 7, 60, 'pvp dps feral (heart of the wild / ns)', 100, '01-500152130320214-05501310231', 5875),
(11, 8, 60, 'pvp dps balance (moonfury)', 100, '510050300250135--50502310401', 5875),
(11, 9, 60, 'pvp dps balance (boomkin)', 100, '0143503002551351--5005021', 5875);

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
