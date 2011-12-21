/*
 * ====================================================================
 *
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 */

package ch.boye.httpclientandroidlib.conn.params;

import ch.boye.httpclientandroidlib.impl.conn.tsccm.ThreadSafeClientConnManager;
import ch.boye.httpclientandroidlib.params.CoreConnectionPNames;

/**
 * Parameter names for connection managers in HttpConn.
 *
 * @since 4.0
 */
@Deprecated
public interface ConnManagerPNames {

    /**
     * Defines the timeout in milliseconds used when retrieving an instance of
     * {@link ch.boye.httpclientandroidlib.conn.ManagedClientConnection} from the
     * {@link ch.boye.httpclientandroidlib.conn.ClientConnectionManager}.
     * <p>
     * This parameter expects a value of type {@link Long}.
     * <p>
     * @deprecated use {@link CoreConnectionPNames#CONNECTION_TIMEOUT}
     */
    @Deprecated
    public static final String TIMEOUT = "http.conn-manager.timeout";

    /**
     * Defines the maximum number of connections per route.
     * This limit is interpreted by client connection managers
     * and applies to individual manager instances.
     * <p>
     * This parameter expects a value of type {@link ConnPerRoute}.
     * <p>
     * @deprecated use {@link ThreadSafeClientConnManager#setMaxForRoute(ch.boye.httpclientandroidlib.conn.routing.HttpRoute, int)},
     *  {@link ThreadSafeClientConnManager#getMaxForRoute(ch.boye.httpclientandroidlib.conn.routing.HttpRoute)}
     */
    @Deprecated
    public static final String MAX_CONNECTIONS_PER_ROUTE = "http.conn-manager.max-per-route";

    /**
     * Defines the maximum number of connections in total.
     * This limit is interpreted by client connection managers
     * and applies to individual manager instances.
     * <p>
     * This parameter expects a value of type {@link Integer}.
     * <p>
     * @deprecated use {@link ThreadSafeClientConnManager#setMaxTotal(int)},
     *  {@link ThreadSafeClientConnManager#getMaxTotal()}
     */
    @Deprecated
    public static final String MAX_TOTAL_CONNECTIONS = "http.conn-manager.max-total";

}
