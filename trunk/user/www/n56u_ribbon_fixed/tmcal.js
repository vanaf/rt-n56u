/*
	Tomato GUI
	Copyright (C) 2006-2009 Jonathan Zarate
	http://www.polarcloud.com/tomato/

	For use with Tomato Firmware only.
	No part of this file may be used without permission.
*/

var tabs = [];
var rx_max, rx_avg;
var tx_max, tx_avg;
var xx_max = 0;
var ifdesc;
var htmReady = 0;
var svgReady = 0;
var updating = 0;
var scaleMode = 0;
var scaleLast = -1;
var drawMode = 0;
var drawLast = -1;
var drawColor = 0;
var drawColorRX = 0;	//Viz add 2010.09
var drawColorTX = 0;	//Viz add 2010.09
var avgMode = 0;
var avgLast = -1;
var colorX = 0;
var colors = [
	['Green &amp; Blue', '#118811', '#6495ed'], ['Blue &amp; Orange', '#003EBA', '#FF9000'],
	['Blue &amp; Red', '#003EDD', '#CC4040'], ['Blue', '#22f', '#225'], ['Gray', '#000', '#999'],
	['Red &amp; Black', '#d00', '#000']];
// Viz add colorRX�BcoloTX 2010.09
// 0Orange 1Blue 2Black 3Red 4Gray  5Green
var colorRX = [ '#FF9000', '#003EBA', '#000000',  '#dd0000', '#999999',  '#118811'];
var colorTX = ['#FF9000', '#003EBA', '#000000',  '#dd0000', '#999999',  '#118811'];

function xpsb(bytes)
{
	return (bytes/1024).toFixed(2) + ' <small>KB/s</small>';
}

function showCTab()
{
	showTab('speed-tab-' + ifdesc);
}

function showSelectedOption(prefix, prev, now)
{
	var e;

	elem.removeClass(prefix + prev, 'selected');	// safe if prev doesn't exist
	if ((e = E(prefix + now)) != null) {
		elem.addClass(e, 'selected');
		e.blur();
	}
}

function showDraw()
{
	if (drawLast == drawMode) return;
	showSelectedOption('draw', drawLast, drawMode);
	drawLast = drawMode;
}

function switchDraw(n)
{
	if ((!svgReady) || (updating)) return;
	drawMode = n;
	showDraw();
	showCTab();
	cookie.set(cprefix + 'draw', drawMode);
}

// Viz add 2010.09  vvvvvvvvvv
function showColor()
{
	E('rx-sel').style.background =colorRX[drawColorRX];
	E('tx-sel').style.background =colorTX[drawColorTX];

}

function switchColorRX(rev)
{
	if ((!svgReady) || (updating)) return;
	
	drawColorRX = rev;
	showColor();
	showCTab();
	//cookie.set(cprefix + 'color', drawColorRX + ',' + colorX);
	//E('rx-sel').style.background-color =colorRX[rev];
	
}

function switchColorTX(rev)
{
	if ((!svgReady) || (updating)) return;
	
	drawColorTX = rev;
	showColor();
	showCTab();
	//cookie.set(cprefix + 'color', drawColorTX + ',' + colorX);
}
// Viz add 2010.09 ^^^^^^^^^^

function showScale()
{
	if (scaleMode == scaleLast) return;
	showSelectedOption('scale', scaleLast, scaleMode);
	scaleLast = scaleMode;
}

function switchScale(n)
{
	scaleMode = n;
	showScale();
	showTab('speed-tab-' + ifdesc);
	cookie.set(cprefix + 'scale', scaleMode);
}

function showAvg()
{
	if (avgMode == avgLast) return;
	showSelectedOption('avg', avgLast, avgMode);
	avgLast = avgMode;
}

function switchAvg(n)
{
	if ((!svgReady) || (updating)) return;
	avgMode = n;
	showAvg();
	showCTab();
	cookie.set(cprefix + 'avg', avgMode);
}

function showTab(name)
{
    var h;
    var max;
    var i;
    var rx, tx;
    var e;

    ifdesc = name.replace('speed-tab-', '');
    cookie.set(cprefix + 'tab', ifdesc, 14);
    tabHigh(name);

    h = speed_history[ifdesc];
    if (!h) return;

    E('rx-current').innerHTML = xpsb(h.rx[h.rx.length - 1] / updateDiv);
    E('rx-avg').innerHTML = xpsb(h.rx_avg);
    E('rx-max').innerHTML = xpsb(h.rx_max);

    E('tx-current').innerHTML = xpsb(h.tx[h.tx.length - 1] / updateDiv);
    E('tx-avg').innerHTML = xpsb(h.tx_avg);
    E('tx-max').innerHTML = xpsb(h.tx_max);

    E('rx-total').innerHTML = scaleSize(h.rx_total);
    E('tx-total').innerHTML = scaleSize(h.tx_total);

    if (svgReady)
    {
        updateSVG(speed_history, ifdesc, max, drawMode,
            colorRX[drawColorRX], colorTX[drawColorTX],
            updateInt, updateMaxL, updateDiv, avgMode, clock);
    }
}

function loadData()
{
	var old;
	var t, e;
	var name;
	var i;
	var changed;

	xx_max = 0;
	old = tabs;
	tabs = [];
	clock = new Date();

	if (!speed_history) {
		speed_history = [];
	}
	else {
		for (var i in speed_history) {
			var h = speed_history[i];
			if ((typeof(h.rx) == 'undefined') || (typeof(h.tx) == 'undefined')) {
				delete speed_history[i];
				continue;
			}

			if (updateReTotal) {
				h.rx_total = h.rx_max = 0;
				h.tx_total = h.tx_max = 0;
				for (j = (h.rx.length - updateMaxL); j < h.rx.length; ++j) {
					t = h.rx[j];
					if (t > h.rx_max) h.rx_max = t;
					h.rx_total += t;
					t = h.tx[j];
					if (t > h.tx_max) h.tx_max = t;
					h.tx_total += t;
				}
				h.rx_avg = h.rx_total / updateMaxL;
				h.tx_avg = h.tx_total / updateMaxL;
			}

			if (updateDiv > 1) {
				h.rx_max /= updateDiv;
				h.tx_max /= updateDiv;
				h.rx_avg /= updateDiv;
				h.tx_avg /= updateDiv;
			}
			if (h.rx_max > xx_max) xx_max = h.rx_max;
			if (h.tx_max > xx_max) xx_max = h.tx_max;

			t = i;
			if (i == "WLAN5")
				t = 'Wireless <small>(5GHz)</small>';
			else if (i == "WLAN2")
				t = 'Wireless <small>(2.4GHz)</small>';
			else if (i == "LAN")
				t = 'Wired LAN';
			else if (i == "WAN")
				t = ' Wired WAN';
			else if (i == 'WWAN') {
				t = ' 3G/LTE';
			}
			
			if (t != i && i != "")
				tabs.push(['speed-tab-' + i, t]);
		}

		tabs = tabs.sort(
			function(a, b) {
				if (a[1] < b[1]) return -1;
				if (a[1] > b[1]) return 1;
				return 0;
			});
	}
	if (tabs.length == old.length) {
		for (i = tabs.length - 1; i >= 0; --i)
			if (tabs[i][0] != old[i][0]) break;
		changed = i > 0;
	}
	else changed = 1;

	if (changed) {
		E('tab-area').innerHTML = _tabCreate.apply(this, tabs);
	}
	if (((name = cookie.get(cprefix + 'tab')) != null) && ((speed_history[name] != undefined))) {
		showTab('speed-tab-' + name);
		return;
	}

	if (tabs.length) showTab(tabs[0][0]);
}

function initData()
{
	if (htmReady) {
		loadData();
	}
}

function initCommon(defAvg, defDrawMode, defDrawColorRX, defDrawColorTX) //Viz modify defDrawColor 2010.09
{
	drawMode = fixInt(cookie.get(cprefix + 'draw'), 0, 1, defDrawMode);
	showDraw();

	var c = nvram.rstats_colors.split(',');
	c = (cookie.get(cprefix + 'color') || '').split(',');

	drawColorRX = defDrawColorRX;
	drawColorTX = defDrawColorTX;		
	showColor();

	scaleMode = fixInt(cookie.get(cprefix + 'scale'), 0, 1, 0);  //cprefix = 'bw_r';
	showScale();

	avgMode = fixInt(cookie.get(cprefix + 'avg'), 1, 10, defAvg);
	showAvg();

	htmReady = 1;
	initData();

}
