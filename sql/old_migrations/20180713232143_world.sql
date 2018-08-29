DROP PROCEDURE IF EXISTS add_migration;
delimiter ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20180713232143');
IF v=0 THEN
INSERT INTO `migrations` VALUES ('20180713232143');
-- Add your query below.


REPLACE INTO `battleground_events` VALUES 
(30, 30, 0, 'Dunbaldar South - Alliance Defender Quest0'),
(30, 30, 1, 'Dunbaldar South - Alliance Defender Quest1'),
(30, 30, 2, 'Dunbaldar South - Alliance Defender Quest2'),
(30, 30, 3, 'Dunbaldar South - Alliance Defender Quest2'),

(30, 31, 0, 'Dunbaldar North - Alliance Defender Quest0'),
(30, 31, 1, 'Dunbaldar North - Alliance Defender Quest1'),
(30, 31, 2, 'Dunbaldar North - Alliance Defender Quest2'),
(30, 31, 3, 'Dunbaldar North - Alliance Defender Quest2'),

(30, 32, 0, 'Icewing Bunker - Alliance Defender Quest0'),
(30, 32, 1, 'Icewing Bunker - Alliance Defender Quest1'),
(30, 32, 2, 'Icewing Bunker - Alliance Defender Quest2'),
(30, 32, 3, 'Icewing Bunker - Alliance Defender Quest2'),

(30, 33, 0, 'Stoneheart Bunker - Alliance Defender Quest0'),
(30, 33, 1, 'Stoneheart Bunker - Alliance Defender Quest1'),
(30, 33, 2, 'Stoneheart Bunker - Alliance Defender Quest2'),
(30, 33, 3, 'Stoneheart Bunker - Alliance Defender Quest2'),

(30, 34, 4, 'Iceblood Tower - Horde Defender Quest0'),
(30, 34, 5, 'Iceblood Tower - Horde Defender Quest1'),
(30, 34, 6, 'Iceblood Tower - Horde Defender Quest2'),
(30, 34, 7, 'Iceblood Tower - Horde Defender Quest3'),

(30, 35, 4, 'Tower Point - Horde Defender Quest0'),
(30, 35, 5, 'Tower Point - Horde Defender Quest1'),
(30, 35, 6, 'Tower Point - Horde Defender Quest2'),
(30, 35, 7, 'Tower Point - Horde Defender Quest3'),

(30, 36, 4, 'Frostwolf east Tower - Horde Defender Quest0'),
(30, 36, 5, 'Frostwolf east Tower - Horde Defender Quest1'),
(30, 36, 6, 'Frostwolf east Tower - Horde Defender Quest2'),
(30, 36, 7, 'Frostwolf east Tower - Horde Defender Quest3'),

(30, 37, 4, 'Frostwolf west Tower - Horde Defender Quest0'),
(30, 37, 5, 'Frostwolf west Tower - Horde Defender Quest1'),
(30, 37, 6, 'Frostwolf west Tower - Horde Defender Quest2'),
(30, 37, 7, 'Frostwolf west Tower - Horde Defender Quest3');

DELETE FROM `creature_battleground` WHERE `guid` IN (150396,150397,150398,150399,150400,150401,150402,150403,150404,150405,150406,150407,150408,150409,150410,150411,150412,150413,150414,150415,150416,150417,150418,150419,
150420,150421,150422,150423,150424,150425,150426,150427,1246608,1246609,1246610,1246611,1246612,1246613,1246614,1246615,1246616,1246617,1246618,1246619,1246620,1246621,1246622,1246623,1246624,1246625,1246626,1246627,1246628,
1246629,1246630,1246631,1246632,1246633,1246634,1246635,1246636,1246637,1246638,1246639,1246640,1246641,1246642,1246643,1246644,1246645,1246646,1246647,1246648,1246649,1246650,1246651,1246652,1246653,1246654,1246655,1246944,
1246945,1246946,1246947,1246948,1246949,1246950,1246951,1246952,1246953,1246954,1246955,1246956,1246957,1246958,1246959,1246960,1246961,1246962,1246963,1246964,1246965,1246966,1246967,1246968,1246969,1246970,1246971,1246972,
1246973,1246974,1246975,1246976,1246977,1246978,1246979,1246980,1246981,1246982,1246983,1246984,1246985,1246986,1246987,1246988,1246989,1246990,1246991);

REPLACE INTO `creature_battleground` (`guid`, `event1`, `event2`) VALUES 
('150396', '30', '0'),
('150397', '30', '0'),
('150398', '30', '0'),
('150399', '30', '0'),
('150400', '31', '0'),
('150401', '31', '0'),
('150402', '31', '0'),
('150403', '31', '0'),
('150404', '32', '0'),
('150405', '32', '0'),
('150406', '32', '0'),
('150407', '32', '0'),
('150408', '33', '0'),
('150409', '33', '0'),
('150410', '33', '0'),
('150411', '33', '0'),
		
('150412', '34', '4'),
('150413', '34', '4'),
('150414', '34', '4'),
('150415', '34', '4'),
('150416', '35', '4'),
('150417', '35', '4'),
('150418', '35', '4'),
('150419', '35', '4'),
('150420', '36', '4'),
('150421', '36', '4'),
('150422', '36', '4'),
('150423', '36', '4'),
('150424', '37', '4'),
('150425', '37', '4'),
('150426', '37', '4'),
('150427', '37', '4'),
		
('1246608', '33', '1'),
('1246609', '33', '1'),
('1246610', '33', '1'),
('1246611', '33', '1'),
('1246612', '32', '1'),
('1246613', '32', '1'),
('1246614', '32', '1'),
('1246615', '32', '1'),
('1246616', '31', '1'),
('1246617', '31', '1'),
('1246618', '31', '1'),
('1246619', '31', '1'),
('1246620', '30', '1'),
('1246621', '30', '1'),
('1246622', '30', '1'),
('1246623', '30', '1'),
		
('1246624', '33', '2'),
('1246625', '33', '2'),
('1246626', '33', '2'),
('1246627', '33', '2'),
('1246628', '32', '2'),
('1246629', '32', '2'),
('1246630', '32', '2'),
('1246631', '32', '2'),
('1246632', '31', '2'),
('1246633', '31', '2'),
('1246634', '31', '2'),
('1246635', '31', '2'),
('1246636', '30', '2'),
('1246637', '30', '2'),
('1246638', '30', '2'),
('1246639', '30', '2'),
		
('1246640', '33', '3'),
('1246641', '33', '3'),
('1246642', '33', '3'),
('1246643', '33', '3'),
('1246644', '32', '3'),
('1246645', '32', '3'),
('1246646', '32', '3'),
('1246647', '32', '3'),
('1246648', '31', '3'),
('1246649', '31', '3'),
('1246650', '31', '3'),
('1246651', '31', '3'),
('1246652', '30', '3'),
('1246653', '30', '3'),
('1246654', '30', '3'),
('1246655', '30', '3'),
		
('1246944', '37', '5'),
('1246945', '37', '5'),
('1246946', '37', '5'),
('1246947', '37', '5'),
('1246948', '36', '5'),
('1246949', '36', '5'),
('1246950', '36', '5'),
('1246951', '36', '5'),
('1246952', '35', '5'),
('1246953', '35', '5'),
('1246954', '35', '5'),
('1246955', '35', '5'),
('1246956', '34', '5'),
('1246957', '34', '5'),
('1246958', '34', '5'),
('1246959', '34', '5'),
		
('1246960', '37', '6'),
('1246961', '37', '6'),
('1246962', '37', '6'),
('1246963', '37', '6'),
('1246964', '36', '6'),
('1246965', '36', '6'),
('1246966', '36', '6'),
('1246967', '36', '6'),
('1246968', '35', '6'),
('1246969', '35', '6'),
('1246970', '35', '6'),
('1246971', '35', '6'),
('1246972', '34', '6'),
('1246973', '34', '6'),
('1246974', '34', '6'),
('1246975', '34', '6'),
		
('1246976', '37', '7'),
('1246977', '37', '7'),
('1246978', '37', '7'),
('1246979', '37', '7'),
('1246980', '36', '7'),
('1246981', '36', '7'),
('1246982', '36', '7'),
('1246983', '36', '7'),
('1246984', '35', '7'),
('1246985', '35', '7'),
('1246986', '35', '7'),
('1246987', '35', '7'),
('1246988', '34', '7'),
('1246989', '34', '7'),
('1246990', '34', '7'),
('1246991', '34', '7');



-- End of migration.
END IF;
END??
delimiter ; 
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;