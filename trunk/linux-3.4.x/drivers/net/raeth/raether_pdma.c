/*
 * PDMA implementation
 * Note: this file is part of raether.c
 */

static void
fe_dma_ring_free(END_DEVICE *ei_local)
{
	int i;

	/* free RX buffers */
	for (i = 0; i < NUM_RX_DESC; i++) {
		if (ei_local->rxd_buff[i]) {
			dev_kfree_skb(ei_local->rxd_buff[i]);
			ei_local->rxd_buff[i] = NULL;
		}
	}

	/* free PDMA RX descriptors */
	if (ei_local->rxd_ring) {
		dma_free_coherent(NULL, NUM_RX_DESC * sizeof(struct PDMA_rxdesc), ei_local->rxd_ring, ei_local->rxd_ring_phy);
		ei_local->rxd_ring = NULL;
	}

	/* free PDMA TX descriptors */
	if (ei_local->txd_ring) {
		dma_free_coherent(NULL, NUM_TX_DESC * sizeof(struct PDMA_txdesc), ei_local->txd_ring, ei_local->txd_ring_phy);
		ei_local->txd_ring = NULL;
	}
}

static int
fe_dma_ring_alloc(END_DEVICE *ei_local)
{
	int i;

	/* allocate PDMA TX descriptors */
	ei_local->txd_ring = dma_alloc_coherent(NULL, NUM_TX_DESC * sizeof(struct PDMA_txdesc), &ei_local->txd_ring_phy, GFP_KERNEL);
	if (!ei_local->txd_ring)
		goto err_cleanup;

	/* allocate PDMA RX descriptors */
	ei_local->rxd_ring = dma_alloc_coherent(NULL, NUM_RX_DESC * sizeof(struct PDMA_rxdesc), &ei_local->rxd_ring_phy, GFP_KERNEL);
	if (!ei_local->rxd_ring)
		goto err_cleanup;

	/* allocate PDMA RX buffers */
	for (i = 0; i < NUM_RX_DESC; i++) {
		ei_local->rxd_buff[i] = dev_alloc_skb(MAX_RX_LENGTH + NET_IP_ALIGN);
		if (!ei_local->rxd_buff[i])
			goto err_cleanup;
#if !defined (RAETH_PDMA_V2)
		skb_reserve(ei_local->rxd_buff[i], NET_IP_ALIGN);
#endif
	}

	return 0;

err_cleanup:
	fe_dma_ring_free(ei_local);
	return -ENOMEM;
}

static void
fe_dma_init(END_DEVICE *ei_local)
{
	int i;
	u32 regVal;

	/* init PDMA TX ring */
	ei_local->txd_free_idx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		ei_local->txd_buff[i] = NULL;
		ei_local->txd_ring[i].txd_info1 = 0;
		ei_local->txd_ring[i].txd_info2 = TX2_DMA_DONE;
		ei_local->txd_ring[i].txd_info3 = 0;
#if defined (RAETH_PDMA_V2)
		ei_local->txd_ring[i].txd_info4 = 0;
#else
		ei_local->txd_ring[i].txd_info4 = TX4_DMA_QN(3);
#endif
	}

	/* init PDMA RX ring */
	for (i = 0; i < NUM_RX_DESC; i++) {
		ei_local->rxd_ring[i].rxd_info1 = (u32)dma_map_single(NULL, ei_local->rxd_buff[i]->data, MAX_RX_LENGTH, DMA_FROM_DEVICE);
		ei_local->rxd_ring[i].rxd_info3 = 0;
		ei_local->rxd_ring[i].rxd_info4 = 0;
#if defined (RAETH_PDMA_V2)
		ei_local->rxd_ring[i].rxd_info2 = RX2_DMA_SDL0_SET(MAX_RX_LENGTH);
#else
		ei_local->rxd_ring[i].rxd_info2 = RX2_DMA_LS0;
#endif
	}

	wmb();

	/* clear PDMA */
	regVal = sysRegRead(PDMA_GLO_CFG);
	regVal &= ~(0x000000FF);
	sysRegWrite(PDMA_GLO_CFG, regVal);

	/* GDMA1/2 <- TX Ring #0 */
	sysRegWrite(TX_BASE_PTR0, phys_to_bus((u32)ei_local->txd_ring_phy));
	sysRegWrite(TX_MAX_CNT0, cpu_to_le32((u32)NUM_TX_DESC));
	sysRegWrite(TX_CTX_IDX0, 0);
	sysRegWrite(PDMA_RST_CFG, PST_DTX_IDX0);

	/* GDMA1/2 -> RX Ring #0 */
	sysRegWrite(RX_BASE_PTR0, phys_to_bus((u32)ei_local->rxd_ring_phy));
	sysRegWrite(RX_MAX_CNT0, cpu_to_le32((u32)NUM_RX_DESC));
	sysRegWrite(RX_CALC_IDX0, cpu_to_le32((u32)(NUM_RX_DESC - 1)));
	sysRegWrite(PDMA_RST_CFG, PST_DRX_IDX0);

	/* only the following chipset need to set it */
#if defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3883)
	//set 1us timer count in unit of clock cycle
	regVal = sysRegRead(FE_GLO_CFG);
	regVal &= ~(0xff << 8); //clear bit8-bit15
	regVal |= (((get_surfboard_sysclk()/1000000)) << 8);
	sysRegWrite(FE_GLO_CFG, regVal);
#endif

	/* config DLY interrupt */
	sysRegWrite(DLY_INT_CFG, FE_DLY_INIT_VALUE);
}

static void
fe_dma_uninit(END_DEVICE *ei_local)
{
	int i;

	/* free uncompleted PDMA TX buffers */
	for (i = 0; i < NUM_TX_DESC; i++) {
		if (ei_local->txd_buff[i]) {
			ei_local->txd_ring[i].txd_info2 = TX2_DMA_DONE;
			ei_local->txd_ring[i].txd_info1 = 0;
#if defined (CONFIG_RAETH_SG_DMA_TX)
			ei_local->txd_ring[i].txd_info3 = 0;
			if (ei_local->txd_buff[i] != (struct sk_buff *)0xFFFFFFFF)
#endif
				dev_kfree_skb(ei_local->txd_buff[i]);
			ei_local->txd_buff[i] = NULL;
		}
	}
	ei_local->txd_free_idx = 0;

	/* uninit PDMA RX ring */
	for (i = 0; i < NUM_RX_DESC; i++) {
		if (ei_local->rxd_ring[i].rxd_info1) {
			ei_local->rxd_ring[i].rxd_info1 = 0;
#if defined (RAETH_PDMA_V2)
			ei_local->rxd_ring[i].rxd_info2 = RX2_DMA_SDL0_SET(MAX_RX_LENGTH);
#else
			ei_local->rxd_ring[i].rxd_info2 = RX2_DMA_LS0;
#endif
		}
	}

	wmb();

	/* clear adapter PDMA TX ring */
	sysRegWrite(TX_BASE_PTR0, 0);
	sysRegWrite(TX_MAX_CNT0, 0);

	/* clear adapter PDMA RX ring */
	sysRegWrite(RX_BASE_PTR0, 0);
	sysRegWrite(RX_MAX_CNT0,  0);
}

static inline int
dma_xmit(struct sk_buff* skb, struct net_device *dev, END_DEVICE *ei_local, int gmac_no)
{
	struct PDMA_txdesc *txd_ring;
	struct netdev_queue *txq;
	u32 i, tx_cpu_owner_idx, tx_cpu_owner_idx_next;
	u32 txd_info4;
#if defined (CONFIG_RAETH_SG_DMA_TX)
	u32 txd_info2, nr_slots, nr_frags;
	const skb_frag_t *tx_frag;
	const struct skb_shared_info *shinfo;
#else
#define nr_slots 1
#endif

	if (!ei_local->active) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

#if defined (CONFIG_RA_HW_NAT) || defined (CONFIG_RA_HW_NAT_MODULE)
	if (ra_sw_nat_hook_tx != NULL) {
#if defined (CONFIG_RA_HW_NAT_WIFI) || defined (CONFIG_RA_HW_NAT_PCI)
		if (IS_DPORT_PPE_VALID(skb))
			gmac_no = PSE_PORT_PPE;
		else
#endif
		if (ra_sw_nat_hook_tx(skb, gmac_no) == 0) {
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
	}
#endif

#if !defined (CONFIG_RALINK_MT7621)
	if (skb->len < ei_local->min_pkt_len) {
		if (skb_padto(skb, ei_local->min_pkt_len)) {
#if defined (CONFIG_RAETH_DEBUG)
			if (net_ratelimit())
				printk(KERN_ERR "%s: skb_padto failed\n", RAETH_DEV_NAME);
#endif
			return NETDEV_TX_OK;
		}
		skb_put(skb, ei_local->min_pkt_len - skb->len);
	}
#endif

#if defined (CONFIG_RALINK_MT7620)
	if (gmac_no == PSE_PORT_PPE)
		txd_info4 = TX4_DMA_FP_BMAP(0x80); /* P7 */
	else
#if defined (CONFIG_RAETH_HAS_PORT5) && !defined (CONFIG_RAETH_HAS_PORT4) && !defined (CONFIG_RAETH_ESW)
		txd_info4 = TX4_DMA_FP_BMAP(0x20); /* P5 */
#elif defined (CONFIG_RAETH_HAS_PORT4) && !defined (CONFIG_RAETH_HAS_PORT5) && !defined (CONFIG_RAETH_ESW)
		txd_info4 = TX4_DMA_FP_BMAP(0x10); /* P4 */
#else
		txd_info4 = 0; /* routing by DA */
#endif
#elif defined (CONFIG_RALINK_MT7621)
	txd_info4 = TX4_DMA_FPORT(gmac_no);
#else
	txd_info4 = (TX4_DMA_QN(3) | TX4_DMA_PN(gmac_no));
#endif

#if defined (CONFIG_RAETH_CHECKSUM_OFFLOAD) && !defined (RAETH_SDMA)
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		txd_info4 |= TX4_DMA_TUI_CO(7);
#endif

#if defined (CONFIG_RAETH_HW_VLAN_TX)
	if (vlan_tx_tag_present(skb)) {
#if defined (CONFIG_RALINK_MT7621)
		txd_info4 |= (0x10000 | vlan_tx_tag_get(skb));
#else
		u32 vlan_tci = vlan_tx_tag_get(skb);
		txd_info4 |= (TX4_DMA_INSV | TX4_DMA_VPRI(vlan_tci));
		txd_info4 |= (u32)vlan_4k_map[(vlan_tci & VLAN_VID_MASK)];
#endif
	}
#endif

#if defined (CONFIG_RAETH_SG_DMA_TX)
	shinfo = skb_shinfo(skb);
	nr_frags = shinfo->nr_frags;
	nr_slots = (nr_frags >> 1) + 1;
#endif

	txq = netdev_get_tx_queue(dev, 0);

	/* protect TX ring access (from eth2/eth3 queues) */
	spin_lock(&ei_local->page_lock);

	tx_cpu_owner_idx = le32_to_cpu(sysRegRead(TX_CTX_IDX0));

	for (i = 0; i <= nr_slots; i++) {
		tx_cpu_owner_idx_next = (tx_cpu_owner_idx + i) % NUM_TX_DESC;
		if (ei_local->txd_buff[tx_cpu_owner_idx_next] ||
		  !(ei_local->txd_ring[tx_cpu_owner_idx_next].txd_info2 & TX2_DMA_DONE)) {
			spin_unlock(&ei_local->page_lock);
			netif_tx_stop_queue(txq);
#if defined (CONFIG_RAETH_DEBUG)
			if (net_ratelimit())
				printk("%s: PDMA TX ring is full! (GMAC: %d)\n", RAETH_DEV_NAME, gmac_no);
#endif
			return NETDEV_TX_BUSY;
		}
	}

#if defined (CONFIG_RAETH_TSO)
	/* fill MSS info in tcp checksum field */
	if (shinfo->gso_size) {
		if (skb_header_cloned(skb)) {
			if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
				spin_unlock(&ei_local->page_lock);
				dev_kfree_skb(skb);
#if defined (CONFIG_RAETH_DEBUG)
				if (net_ratelimit())
					printk(KERN_ERR "%s: pskb_expand_head for TSO failed!\n", RAETH_DEV_NAME);
#endif
				return NETDEV_TX_OK;
			}
		}
		if (shinfo->gso_type & (SKB_GSO_TCPV4|SKB_GSO_TCPV6)) {
			u32 hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (skb->len > hdr_len) {
				tcp_hdr(skb)->check = htons(shinfo->gso_size);
				txd_info4 |= TX4_DMA_TSO;
			}
		}
	}
#endif

	ei_local->txd_buff[tx_cpu_owner_idx] = skb;

	/* write DMA TX desc (DDONE must be cleared last) */
	txd_ring = &ei_local->txd_ring[tx_cpu_owner_idx];

	txd_ring->txd_info4 = txd_info4;
#if defined (CONFIG_RAETH_SG_DMA_TX)
	if (nr_frags) {
		txd_ring->txd_info1 = (u32)dma_map_single(NULL, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
		
		txd_info2 = TX2_DMA_SDL0(skb_headlen(skb));
		for (i = 0; i < nr_frags; i++) {
			tx_frag = &shinfo->frags[i];
			if (i % 2) {
				tx_cpu_owner_idx = (tx_cpu_owner_idx + 1) % NUM_TX_DESC;
				ei_local->txd_buff[tx_cpu_owner_idx] = (struct sk_buff *)0xFFFFFFFF; //MAGIC ID
				txd_ring = &ei_local->txd_ring[tx_cpu_owner_idx];
				
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
				txd_ring->txd_info1 = (u32)skb_frag_dma_map(NULL, tx_frag, 0, skb_frag_size(tx_frag), DMA_TO_DEVICE);
#else
				txd_ring->txd_info1 = (u32)dma_map_page(NULL, tx_frag->page, tx_frag->page_offset, tx_frag->size, DMA_TO_DEVICE);
#endif
				txd_ring->txd_info4 = txd_info4;
				if ((i + 1) == nr_frags) { // last segment
					txd_ring->txd_info3 = 0;
					txd_ring->txd_info2 = (TX2_DMA_SDL0(tx_frag->size) | TX2_DMA_LS0);
				} else
					txd_info2 = TX2_DMA_SDL0(tx_frag->size);
			} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
				txd_ring->txd_info3 = (u32)skb_frag_dma_map(NULL, tx_frag, 0, skb_frag_size(tx_frag), DMA_TO_DEVICE);
#else
				txd_ring->txd_info3 = (u32)dma_map_page(NULL, tx_frag->page, tx_frag->page_offset, tx_frag->size, DMA_TO_DEVICE);
#endif
				if ((i + 1) == nr_frags) // last segment
					txd_info2 |= (TX2_DMA_SDL1(tx_frag->size) | TX2_DMA_LS1);
				else
					txd_info2 |= (TX2_DMA_SDL1(tx_frag->size));
				
				txd_ring->txd_info2 = txd_info2;
			}
		}
	} else
#endif
	{
		txd_ring->txd_info1 = (u32)dma_map_single(NULL, skb->data, skb->len, DMA_TO_DEVICE);
#if defined (CONFIG_RAETH_SG_DMA_TX)
		txd_ring->txd_info3 = 0;
#endif
		txd_ring->txd_info2 = (TX2_DMA_SDL0(skb->len) | TX2_DMA_LS0);
	}

#if defined (CONFIG_RAETH_BQL)
	netdev_tx_sent_queue(txq, skb->len);
#endif

	wmb();

	/* kick the DMA TX */
	sysRegWrite(TX_CTX_IDX0, cpu_to_le32(tx_cpu_owner_idx_next));

	spin_unlock(&ei_local->page_lock);

	return NETDEV_TX_OK;
}

static inline void
dma_xmit_clean(struct net_device *dev, END_DEVICE *ei_local)
{
	struct netdev_queue *txq;
	struct sk_buff *txd_buff;
	int cpu, clean_done = 0;
	u32 txd_free_idx;
#if defined (CONFIG_RAETH_BQL)
	u32 bytes_sent_ge1 = 0;
#if defined (CONFIG_PSEUDO_SUPPORT)
	u32 bytes_sent_ge2 = 0;
#endif
#endif

	spin_lock(&ei_local->page_lock);

	txd_free_idx = ei_local->txd_free_idx;

	while (clean_done < NUM_TX_DESC) {
		txd_buff = ei_local->txd_buff[txd_free_idx];
		
		if (!txd_buff || !(ei_local->txd_ring[txd_free_idx].txd_info2 & TX2_DMA_DONE))
			break;
		
		clean_done++;
		
#if defined (CONFIG_RAETH_SG_DMA_TX)
		if (txd_buff != (struct sk_buff *)0xFFFFFFFF)
#endif
		{
#if defined (CONFIG_RAETH_BQL)
#if defined (CONFIG_PSEUDO_SUPPORT)
			if (txd_buff->dev == ei_local->PseudoDev)
				bytes_sent_ge2 += txd_buff->len;
			else
#endif
				bytes_sent_ge1 += txd_buff->len;
#endif
			dev_kfree_skb(txd_buff);
		}
		ei_local->txd_buff[txd_free_idx] = NULL;
		txd_free_idx = (txd_free_idx + 1) % NUM_TX_DESC;
	}

	if (ei_local->txd_free_idx != txd_free_idx)
		ei_local->txd_free_idx = txd_free_idx;

	spin_unlock(&ei_local->page_lock);

	if (!clean_done)
		return;

	cpu = smp_processor_id();

	if (netif_running(dev)) {
		txq = netdev_get_tx_queue(dev, 0);
		__netif_tx_lock(txq, cpu);
#if defined (CONFIG_RAETH_BQL)
		netdev_tx_completed_queue(txq, 0, bytes_sent_ge1);
#endif
		if (netif_tx_queue_stopped(txq))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}

#if defined (CONFIG_PSEUDO_SUPPORT)
	if (netif_running(ei_local->PseudoDev)) {
		txq = netdev_get_tx_queue(ei_local->PseudoDev, 0);
		__netif_tx_lock(txq, cpu);
#if defined (CONFIG_RAETH_BQL)
		netdev_tx_completed_queue(txq, 0, bytes_sent_ge2);
#endif
		if (netif_tx_queue_stopped(txq))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}
#endif
}

