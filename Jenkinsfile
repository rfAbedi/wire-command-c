pipeline {
    agent any

    options {
        skipDefaultCheckout(true)
        timeout(time: 30, unit: 'MINUTES')
    }

    stages {
        stage('Checkout') {
            options {
                timeout(time: 2, unit: 'MINUTES')
            }
            steps {
                checkout scm
            }
        }

        stage('Normal Build') {
            options {
                timeout(time: 3, unit: 'MINUTES')
            }
            steps {
                sh 'make ci-clean'
                sh 'make > build.log 2>&1'
            }
        }

        stage('Unit Tests') {
            options {
                timeout(time: 5, unit: 'MINUTES')
            }
            steps {
                sh 'make test > unit-tests.log 2>&1'
            }
        }

        stage('Integration Tests') {
            options {
                timeout(time: 5, unit: 'MINUTES')
            }
            steps {
                sh 'make integration-test > integration-tests.log 2>&1'
            }
        }

        stage('AddressSanitizer') {
            options {
                timeout(time: 8, unit: 'MINUTES')
            }
            steps {
                sh 'make asan > asan.log 2>&1'
            }
        }

        stage('UndefinedBehaviorSanitizer') {
            options {
                timeout(time: 8, unit: 'MINUTES')
            }
            steps {
                sh 'make ubsan > ubsan.log 2>&1'
            }
        }

        stage('Archive Artifacts and Reports') {
            options {
                timeout(time: 2, unit: 'MINUTES')
            }
            steps {
                archiveArtifacts artifacts: 'wirecommand,wirecommand-uds,wirecommand-uds-threaded,*.log',
                                 fingerprint: true
            }
        }
    }

    post {
        always {
            archiveArtifacts artifacts: '*.log', allowEmptyArchive: true
            sh 'make ci-clean'
        }
    }
}
